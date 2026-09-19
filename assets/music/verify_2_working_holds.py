#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_2_working_holds.py -- independent re-parser for the working-holds MIDIs.

Shares no code with gen_2_working_holds.py on purpose: it decodes the bytes on
disc from scratch and asserts the properties the brief and
PROVENANCE/TVUL_MUSIC_FORMAT.md sec.7.2 require.

  1. MThd is well formed: format 1, ntrks matches, division 480
  2. every MTrk length field matches its actual body
  3. every track ends with FF 2F 00 and nothing follows it
  4. every note-on has a matching note-off (velocity-0 note-on counts as off)
  5. nothing is still sounding at the loop point -> the loop cannot click
  6. no track is empty
  7. tempo, time signature, CC#111 loop marker and a loop text meta are present
  8. total duration is what was intended

Usage: python3 verify_2_working_holds.py
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

EXPECT = {
    "bellowsworks_anvil_thirteen.mid": {"bpm": 132, "bars": 34, "div": 480},
    "gildmaw_exchange_open_outcry.mid": {"bpm": 138, "bars": 55, "div": 480},
    "vellumscale_archive_slow_accretion.mid": {"bpm": 60, "bars": 55, "div": 480},
}

META_NAMES = {0x01: "text", 0x03: "trkname", 0x06: "marker",
              0x2F: "eot", 0x51: "tempo", 0x58: "timesig", 0x59: "keysig"}


class Reader(object):
    def __init__(self, buf):
        self.b = buf
        self.i = 0

    def u8(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def take(self, n):
        v = self.b[self.i:self.i + n]
        if len(v) != n:
            raise ValueError("truncated")
        self.i += n
        return v

    def vlq(self):
        v = 0
        for _ in range(4):
            c = self.u8()
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80:
                return v
        raise ValueError("vlq too long")


def parse(path):
    raw = open(path, "rb").read()
    problems = []
    if raw[:4] != b"MThd":
        return None, ["missing MThd"]
    hlen, fmt, ntrks, div = struct.unpack(">IHHH", raw[4:14])
    if hlen != 6:
        problems.append("MThd length %d != 6" % hlen)
    if fmt != 1:
        problems.append("format %d != 1" % fmt)
    if div != 480:
        problems.append("division %d != 480" % div)

    pos = 8 + hlen
    tracks = []
    while pos < len(raw):
        if raw[pos:pos + 4] != b"MTrk":
            problems.append("expected MTrk at offset %d" % pos)
            break
        tlen = struct.unpack(">I", raw[pos + 4:pos + 8])[0]
        body = raw[pos + 8:pos + 8 + tlen]
        if len(body) != tlen:
            problems.append("MTrk at %d truncated (%d < %d)" % (pos, len(body), tlen))
        tracks.append(body)
        pos += 8 + tlen
    if pos != len(raw):
        problems.append("%d trailing bytes after last MTrk" % (len(raw) - pos))
    if ntrks != len(tracks):
        problems.append("ntrks %d != %d MTrk chunks" % (ntrks, len(tracks)))

    info = {"div": div, "ntrks": ntrks, "tracks": []}
    for ti, body in enumerate(tracks):
        r = Reader(body)
        tick = 0
        running = None
        open_notes = {}
        note_ons = 0
        note_offs = 0
        events = 0
        saw_eot = False
        eot_tick = None
        name = ""
        metas = []
        ccs = []
        tempo = None
        tsig = None
        max_off = 0
        dangling = []
        while r.i < len(body):
            tick += r.vlq()
            b0 = r.u8()
            if b0 < 0x80:
                if running is None:
                    problems.append("trk%d: running status with no status byte" % ti)
                    break
                status = running
                r.i -= 1
            else:
                status = b0
                if b0 < 0xF0:
                    running = b0
            events += 1
            if status == 0xFF:
                mtype = r.u8()
                ln = r.vlq()
                data = r.take(ln)
                metas.append(mtype)
                if mtype == 0x2F:
                    saw_eot = True
                    eot_tick = tick
                    if r.i != len(body):
                        problems.append("trk%d: %d bytes after FF 2F 00"
                                        % (ti, len(body) - r.i))
                    break
                if mtype == 0x03:
                    name = data.decode("ascii", "replace")
                if mtype == 0x51:
                    tempo = struct.unpack(">I", b"\x00" + data)[0]
                if mtype == 0x58:
                    tsig = (data[0], 1 << data[1])
            elif status in (0xF0, 0xF7):
                ln = r.vlq()
                r.take(ln)
            else:
                hi = status & 0xF0
                ch = status & 0x0F
                if hi in (0xC0, 0xD0):
                    r.u8()
                elif hi == 0x90:
                    p = r.u8()
                    v = r.u8()
                    if v == 0:
                        key = (ch, p)
                        note_offs += 1
                        if key in open_notes and open_notes[key]:
                            open_notes[key].pop()
                            max_off = max(max_off, tick)
                        else:
                            problems.append("trk%d: note-off with no note-on ch%d p%d @%d"
                                            % (ti, ch, p, tick))
                    else:
                        note_ons += 1
                        open_notes.setdefault((ch, p), []).append(tick)
                elif hi == 0x80:
                    p = r.u8()
                    r.u8()
                    key = (ch, p)
                    note_offs += 1
                    if key in open_notes and open_notes[key]:
                        open_notes[key].pop()
                        max_off = max(max_off, tick)
                    else:
                        problems.append("trk%d: note-off with no note-on ch%d p%d @%d"
                                        % (ti, ch, p, tick))
                else:
                    b1 = r.u8()
                    b2 = r.u8()
                    if hi == 0xB0:
                        ccs.append((tick, ch, b1, b2))
        for key, stack in open_notes.items():
            for st in stack:
                dangling.append((key[0], key[1], st))
        if dangling:
            problems.append("trk%d: %d DANGLING note-on(s), first ch%d p%d @%d"
                            % (ti, len(dangling), dangling[0][0], dangling[0][1],
                               dangling[0][2]))
        if not saw_eot:
            problems.append("trk%d: missing FF 2F 00" % ti)
        if events <= 1:
            problems.append("trk%d: empty track" % ti)
        info["tracks"].append({
            "idx": ti, "name": name, "events": events, "ons": note_ons,
            "offs": note_offs, "eot": eot_tick, "max_off": max_off,
            "tempo": tempo, "tsig": tsig, "ccs": ccs, "metas": metas,
            "dangling": len(dangling),
        })
    return info, problems


def main():
    rc = 0
    for fname in sorted(EXPECT):
        path = os.path.join(HERE, fname)
        exp = EXPECT[fname]
        print("=" * 78)
        print(fname)
        if not os.path.exists(path):
            print("  MISSING")
            rc = 1
            continue
        info, problems = parse(path)
        if info is None:
            print("  UNPARSEABLE:", problems)
            rc = 1
            continue

        loop_ticks = exp["bars"] * 4 * exp["div"]
        tot_ons = sum(t["ons"] for t in info["tracks"])
        tot_offs = sum(t["offs"] for t in info["tracks"])
        tempos = [t["tempo"] for t in info["tracks"] if t["tempo"]]
        tsigs = [t["tsig"] for t in info["tracks"] if t["tsig"]]
        cc111 = [c for t in info["tracks"] for c in t["ccs"] if c[2] == 111]
        has_text = any(0x01 in t["metas"] for t in info["tracks"])
        has_marker = any(0x06 in t["metas"] for t in info["tracks"])

        print("  header       format 1, ntrks %d, division %d" % (info["ntrks"], info["div"]))
        for t in info["tracks"]:
            print("  trk%d %-24s ev %5d  on %4d  off %4d  eot@%-7d last-off@%-7d"
                  % (t["idx"], t["name"][:24], t["events"], t["ons"], t["offs"],
                     t["eot"] if t["eot"] is not None else -1, t["max_off"]))
        if tempos:
            bpm = 60000000.0 / tempos[0]
            print("  tempo        %.3f BPM (expected %d)" % (bpm, exp["bpm"]))
            if abs(bpm - exp["bpm"]) > 0.5:
                problems.append("tempo %.2f != expected %d" % (bpm, exp["bpm"]))
        else:
            problems.append("no tempo meta")
        if tsigs:
            print("  time sig     %d/%d" % tsigs[0])
        else:
            problems.append("no time-signature meta")

        eots = set(t["eot"] for t in info["tracks"])
        print("  loop length  %d ticks = %d bars; EOT ticks %s"
              % (loop_ticks, exp["bars"], sorted(eots)))
        if eots != {loop_ticks}:
            problems.append("EOT ticks %s != loop length %d" % (sorted(eots), loop_ticks))

        latest = max(t["max_off"] for t in info["tracks"])
        gap = loop_ticks - latest
        print("  last note-off @%d  -> %d ticks (%.0f ms) of air before the loop"
              % (latest, gap, gap / float(info["div"]) * 60000.0 / exp["bpm"]))
        if latest > loop_ticks:
            problems.append("a note sounds past the loop point")

        secs = loop_ticks / float(info["div"]) * 60.0 / exp["bpm"]
        print("  duration     %.2f s  (%d:%05.2f)" % (secs, int(secs // 60), secs % 60))
        print("  notes        %d on / %d off  -> %s"
              % (tot_ons, tot_offs, "BALANCED" if tot_ons == tot_offs else "MISMATCH"))
        if tot_ons != tot_offs:
            problems.append("note on/off mismatch %d/%d" % (tot_ons, tot_offs))
        dang = sum(t["dangling"] for t in info["tracks"])
        print("  dangling     %d" % dang)
        print("  CC#111       %s" % ("present @tick %d ch%d" % (cc111[0][0], cc111[0][1])
                                     if cc111 else "ABSENT"))
        if not cc111:
            problems.append("no CC#111 loop marker")
        if not has_text:
            problems.append("no text meta")
        if not has_marker:
            problems.append("no marker meta")
        print("  text/marker  text=%s marker=%s" % (has_text, has_marker))

        if problems:
            rc = 1
            print("  RESULT: FAIL")
            for p in problems:
                print("    - " + p)
        else:
            print("  RESULT: PASS")
    print("=" * 78)
    print("overall: %s" % ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
