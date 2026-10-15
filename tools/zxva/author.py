#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""author.py - compile a hand-written sound (.zxvas text) to ZXVA.

    author.py in.zxvas out.zxva

Interface sounds and chimes are authored directly as vectors, no recording.
One statement per line, `#` starts a comment, indented `bp` / `frame` lines
belong to the statement above. Units: times take s, ms or us (bare numbers
are microseconds), frequencies Hz or k (1.5k), levels dB (-6dB, -inf) or
linear (0.5), pan 0..1 (left..right).

    zxvas 1
    channels 2
    duration 1.5s
    seed 42
    norm peak=<Q40 int> target=<Q40 int>
    partial [ch=0] [pan=0] [locked=1]
      bp <t> <f> <level> [phase_turns]
    noise t=<t> hop=<t> edges=<Hz,Hz,...> [ch] [pan]
      frame <level> <level> ...          (one level per band)
    grain t=<t> dur=<t> f=<f> amp=<level> [kind=tone|noise] [bw=<f>]
          [attack=<t>] [half=<t>] [phase=<turns>] [ch] [pan]
    voice modal t=<t> f=<f> env=<t>:<level>,... partials=<ratio>:<level>:<half>,...
    voice fm    t=<t> f=<f> env=... ratio=<r> index=<i0>:<i1>
    voice pluck t=<t> f=<f> env=... half=<t> damp=<0..1> pos=<0..0.5>
    fvoice tone|noise|sweep rate=<Hz> start=<frames> len=<frames>
          gains=<Q30>,<Q30> [env=none|asr:A:R|halfsine|ad:A:dec] [echo=n:delay:gainQ30[:mirror]]
          tone:  partials=<mHz>:<ampQ14>:<attack>:<decQ30>,...
          noise: seed= mul= add= lp=<alphaQ16> hp=<alphaQ16> amp=<Q14>
          sweep: f0=<mHz> glide=<Q30> amp=<Q14>
    [ch=<n>] [pan=<0..1>] are accepted on every sound statement.

The format is specified in kernel/src/vaudio/ZXVA.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zxvafmt as Z  # noqa: E402


class Err(Exception):
    pass


def t_us(s):
    s = s.strip()
    for suf, m in (("us", 1), ("ms", 1000), ("s", 1000000)):
        if s.endswith(suf):
            return int(round(float(s[:-len(suf)]) * m))
    return int(round(float(s)))


def hz(s):
    s = s.strip().lower()
    if s.endswith("hz"):
        s = s[:-2]
    if s.endswith("k"):
        return float(s[:-1]) * 1000
    return float(s)


def level(s):
    s = s.strip().lower()
    if s in ("-inf", "-infdb", "0", "off"):
        return 0.0
    if s.endswith("db"):
        return 10 ** (float(s[:-2]) / 20)
    return float(s)


def kv(tokens):
    out, pos = {}, []
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            out[k] = v
        else:
            pos.append(t)
    return out, pos


def place(o):
    return int(o.get("ch", 0)), int(round(float(o.get("pan", 0)) * 65536))


def compile_text(text):
    head = {"channels": 1, "duration": None, "seed": 0}
    items = []
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        tok = line.split()
        if raw[:1] in " \t":
            if not items:
                raise Err("line %d: indented line with no statement" % ln)
            items[-1][2].append((ln, tok))
            continue
        items.append((ln, tok, []))
    w = None
    for ln, tok, sub in items:
        try:
            cmd = tok[0]
            if cmd == "zxvas":
                if tok[1:] != ["1"]:
                    raise Err("unknown zxvas version")
                continue
            if cmd in ("channels", "duration", "seed"):
                head[cmd] = t_us(tok[1]) if cmd == "duration" else int(tok[1])
                continue
            if w is None:
                if head["duration"] is None:
                    raise Err("duration must come before the first sound")
                w = Z.Writer(head["channels"], head["duration"], head["seed"])
            o, pos = kv(tok[1:])
            ch, pan = place(o)
            if cmd == "norm":
                w.norm(int(o["peak"]), int(o["target"]))
            elif cmd == "partial":
                bps = []
                for _, s in sub:
                    if s[0] != "bp":
                        raise Err("expected bp")
                    bps.append((t_us(s[1]), hz(s[2]), level(s[3]),
                                float(s[4]) if len(s) > 4 else 0.0))
                w.partial(bps, ch=ch, pan=pan, locked=o.get("locked", "0") == "1")
            elif cmd == "noise":
                edges = [int(hz(e)) for e in o["edges"].split(",")]
                frames = [[level(v) for v in s[1:]] for _, s in sub]
                if any(len(f) != len(edges) - 1 for f in frames):
                    raise Err("a frame needs one level per band")
                w.noise(t_us(o.get("t", "0")), t_us(o["hop"]), edges, frames, ch=ch, pan=pan)
            elif cmd == "grain":
                w.grain(t_us(o["t"]), t_us(o["dur"]), hz(o["f"]), level(o["amp"]),
                        kind=1 if o.get("kind", "tone") == "noise" else 0,
                        bw=hz(o.get("bw", "0")), attack=t_us(o.get("attack", "0")),
                        half=t_us(o.get("half", "0")), phase=float(o.get("phase", 0)), ch=ch, pan=pan)
            elif cmd == "voice":
                kind = pos[0]
                env = [(t_us(p.split(":")[0]), level(p.split(":")[1])) for p in o["env"].split(",")]
                t0, f = t_us(o.get("t", "0")), hz(o["f"])
                if kind == "modal":
                    parts = []
                    for p in o["partials"].split(","):
                        r, lv, h = (p.split(":") + ["0"])[:3]
                        parts.append((float(r), level(lv), t_us(h)))
                    w.voice_modal(t0, f, env, parts, ch=ch, pan=pan)
                elif kind == "fm":
                    i0, i1 = (o.get("index", "1:1").split(":") + ["0"])[:2]
                    w.voice_fm(t0, f, env, float(o["ratio"]), float(i0), float(i1), ch=ch, pan=pan)
                elif kind == "pluck":
                    w.voice_pluck(t0, f, env, t_us(o["half"]), float(o.get("damp", 0.3)),
                                  float(o.get("pos", 0.13)), ch=ch, pan=pan)
                else:
                    raise Err("unknown voice %s" % kind)
            elif cmd == "fvoice":
                kind = {"tone": 0, "noise": 1, "sweep": 2}[pos[0]]
                g = [int(v) for v in o["gains"].split(",")]
                e = o.get("env", "none").split(":")
                env = {"none": (0, 0, 0), "halfsine": (2, 0, 0)}.get(e[0])
                if env is None:
                    env = (1 if e[0] == "asr" else 3, int(e[1]), int(e[2]))
                echo = None
                if "echo" in o:
                    p = o["echo"].split(":")
                    echo = (int(p[0]), int(p[1]), int(p[2]), len(p) > 3 and p[3] == "mirror")
                kw = {}
                if kind == 0:
                    kw["partials"] = [tuple(int(v) for v in p.split(":"))
                                      for p in o["partials"].split(",")]
                elif kind == 1:
                    kw["noise"] = tuple(int(o[k]) for k in ("seed", "mul", "add", "lp", "hp", "amp"))
                else:
                    kw["sweep"] = (int(o["f0"]), int(o["glide"]), int(o["amp"]))
                w.fvoice(int(o["rate"]), int(o.get("start", 0)), int(o["len"]), kind, g, env=env,
                         echo=echo, ch=ch, **kw)
            else:
                raise Err("unknown statement %s" % cmd)
        except (KeyError, IndexError, ValueError) as ex:
            raise Err("line %d: %s" % (ln, ex))
        except Err as ex:
            raise Err("line %d: %s" % (ln, ex))
    if w is None:
        raise Err("no sounds")
    return w.bytes()


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: author.py in.zxvas out.zxva\n")
        return 2
    try:
        data = compile_text(open(argv[0]).read())
    except Err as ex:
        sys.stderr.write("author.py: %s: %s\n" % (argv[0], ex))
        return 1
    open(argv[1], "wb").write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
