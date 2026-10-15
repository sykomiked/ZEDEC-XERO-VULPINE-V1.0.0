#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""svg2zxvg.py - convert an SVG subset to ZXVG, the zui native vector format.

    svg2zxvg.py in.svg out.zxvg [--c name out.inc]

Every graphic in the system is vector: icons, logos, boot and launch screens.
This tool turns an SVG drawn in any editor into the compact binary that
kernel/src/zui/zui_vec.c draws at any size, integer only, on bare metal. The
format is documented in kernel/src/zui/zui_vec.h; this file must agree with it.

Supported: <svg viewBox> (required), <g>, <path d> (every command, absolute and
relative, arcs included), <rect> (rx/ry), <circle>, <ellipse>, <line>,
<polyline>, <polygon>, <defs>, <linearGradient>, <radialGradient> (focal point
at the centre), <stop>; attributes and style="" properties fill, stroke,
stroke-width, stroke-linejoin, stroke-linecap, stroke-miterlimit,
stroke-dasharray, stroke-dashoffset, opacity, fill-opacity, stroke-opacity,
fill-rule, color, transform, display, visibility, id. <title>, <desc> and
<metadata> are ignored (they draw nothing), as are elements in editor
namespaces. Every id is kept: an element with an id becomes a ZXVG group of
that name so zui_kinetic can move it.

Refused loudly (exit 1, nothing written): text, images, filters, masks, clip
paths, patterns, <use>, <style>, <foreignObject>, markers, blend modes, unit
suffixes other than px, and anything else not listed above. A converted file
never silently differs from the drawing.

Python standard library only.
"""

import math
import re
import struct
import sys
import xml.etree.ElementTree as ET

SVG_NS = "http://www.w3.org/2000/svg"
XLINK_NS = "http://www.w3.org/1999/xlink"

VERSION = 1
MAX_PAL = 256
MAX_GRAD = 64
MAX_IDS = 64
MAX_ID_LEN = 31
MAX_STOPS = 8
MAX_DASH = 8
MAX_DEPTH = 16
COORD_LIM = 1 << 23  # |coordinate| in 1/16 units
FIX_LIM = 1 << 30  # |16.16 matrix element|

OP_END, OP_MOVE, OP_LINE, OP_QUAD, OP_CUBIC, OP_ARC, OP_CLOSE, OP_NEWPATH = 0, 1, 2, 3, 4, 5, 6, 7
OP_FILL, OP_STROKE, OP_GROUP, OP_ENDGROUP = 0x10, 0x11, 0x20, 0x21

ID_RE = re.compile(r"^[A-Za-z0-9_.:-]{1,31}$")


class Refuse(Exception):
    pass


def refuse(msg):
    raise Refuse(msg)


# ---- encoding --------------------------------------------------------------
def uv(n):
    if n < 0 or n > 0xFFFFFFFF:
        refuse("value out of range: %d" % n)
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def zz(n):
    if n < -(1 << 31) or n >= (1 << 31):
        refuse("value out of range: %d" % n)
    return uv((n << 1) ^ (n >> 31) if n >= 0 else ((-n) << 1) - 1)


def q4(v):
    r = int(math.floor(v * 16.0 + 0.5))
    if abs(r) >= COORD_LIM:
        refuse("coordinate %g too large (limit %d user units)" % (v, COORD_LIM // 16))
    return r


def fix(v, what):
    r = int(math.floor(v * 65536.0 + 0.5))
    if abs(r) >= FIX_LIM:
        refuse("%s %g out of range (limit 16384)" % (what, v))
    return r


# ---- affine matrices: (a, b, c, d, e, f) as in SVG -------------------------
IDENT = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mul(m, n):  # m * n: apply n first, then m
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return (a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D, a * E + c * F + e,
            b * E + d * F + f)


def apply(m, x, y):
    return (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])


NUM = r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?"


def parse_transform(s):
    m = IDENT
    s = s.strip()
    pos = 0
    pat = re.compile(r"\s*,?\s*([A-Za-z]+)\s*\(([^)]*)\)\s*")
    while pos < len(s):
        mt = pat.match(s, pos)
        if not mt:
            refuse("cannot parse transform %r" % s)
        name, args = mt.group(1), [float(x) for x in re.findall(NUM, mt.group(2))]
        pos = mt.end()
        if name == "matrix" and len(args) == 6:
            t = tuple(args)
        elif name == "translate" and len(args) in (1, 2):
            t = (1, 0, 0, 1, args[0], args[1] if len(args) == 2 else 0.0)
        elif name == "scale" and len(args) in (1, 2):
            t = (args[0], 0, 0, args[1] if len(args) == 2 else args[0], 0, 0)
        elif name == "rotate" and len(args) in (1, 3):
            r = math.radians(args[0])
            t = (math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0)
            if len(args) == 3:
                t = mul(mul((1, 0, 0, 1, args[1], args[2]), t), (1, 0, 0, 1, -args[1], -args[2]))
        elif name == "skewX" and len(args) == 1:
            t = (1, 0, math.tan(math.radians(args[0])), 1, 0, 0)
        elif name == "skewY" and len(args) == 1:
            t = (1, math.tan(math.radians(args[0])), 0, 1, 0, 0)
        else:
            refuse("unsupported transform %s(%s)" % (name, mt.group(2)))
        m = mul(m, t)
    return m


# ---- lengths and colours ---------------------------------------------------
def length(v, what):
    v = v.strip()
    mt = re.fullmatch(r"(%s)(px)?" % NUM, v)
    if not mt:
        refuse("unsupported length %r for %s (plain numbers or px only)" % (v, what))
    return float(mt.group(1))


NAMED = {
    "black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0), "green": (0, 128, 0),
    "lime": (0, 255, 0), "blue": (0, 0, 255), "yellow": (255, 255, 0), "cyan": (0, 255, 255),
    "aqua": (0, 255, 255), "magenta": (255, 0, 255), "fuchsia": (255, 0, 255),
    "gray": (128, 128, 128), "grey": (128, 128, 128), "silver": (192, 192, 192),
    "maroon": (128, 0, 0), "olive": (128, 128, 0), "navy": (0, 0, 128), "purple": (128, 0, 128),
    "teal": (0, 128, 128), "orange": (255, 165, 0), "gold": (255, 215, 0),
}


def colour(v, cur):
    """-> (r, g, b, a) with a 0..1, or None for 'none', or ('url', id)."""
    v = v.strip()
    if v == "none" or v == "transparent":
        return None
    if v.startswith("url("):
        mt = re.match(r"url\(\s*['\"]?#([^)'\"]+)['\"]?\s*\)\s*(.*)$", v)
        if not mt:
            refuse("unsupported paint %r" % v)
        return ("url", mt.group(1))
    if v == "currentColor":
        if cur is None:
            refuse("currentColor without a color property")
        return colour(cur, None)
    if v.startswith("#"):
        h = v[1:]
        if re.fullmatch(r"[0-9a-fA-F]{3,4}", h):
            p = [int(c * 2, 16) for c in h]
        elif re.fullmatch(r"[0-9a-fA-F]{6}|[0-9a-fA-F]{8}", h):
            p = [int(h[i:i + 2], 16) for i in range(0, len(h), 2)]
        else:
            refuse("bad colour %r" % v)
        return (p[0], p[1], p[2], p[3] / 255.0 if len(p) == 4 else 1.0)
    mt = re.fullmatch(r"rgba?\(([^)]*)\)", v)
    if mt:
        parts = [x.strip() for x in re.split(r"[,\s/]+", mt.group(1).strip()) if x.strip()]
        if len(parts) not in (3, 4):
            refuse("bad colour %r" % v)
        ch = []
        for x in parts[:3]:
            ch.append(int(round(float(x[:-1]) * 2.55)) if x.endswith("%") else int(round(float(x))))
        a = 1.0
        if len(parts) == 4:
            a = float(parts[3][:-1]) / 100 if parts[3].endswith("%") else float(parts[3])
        return (max(0, min(255, ch[0])), max(0, min(255, ch[1])), max(0, min(255, ch[2])),
                max(0.0, min(1.0, a)))
    if v.lower() in NAMED:
        r, g, b = NAMED[v.lower()]
        return (r, g, b, 1.0)
    refuse("unsupported colour %r" % v)


def number01(v, what):
    v = v.strip()
    try:
        x = float(v[:-1]) / 100 if v.endswith("%") else float(v)
    except ValueError:
        refuse("bad %s %r" % (what, v))
    return max(0.0, min(1.0, x))


# ---- path data -------------------------------------------------------------
TOKEN = re.compile(r"([MmLlHhVvCcSsQqTtAaZz])|(%s)|([\s,]+)" % NUM)


def parse_d(d):
    """-> list of absolute segments: ('M',x,y) ('L',x,y) ('Q',cx,cy,x,y)
    ('C',c1x,c1y,c2x,c2y,x,y) ('A',rx,ry,rot,large,sweep,x,y) ('Z',)."""
    toks = []
    pos = 0
    while pos < len(d):
        mt = TOKEN.match(d, pos)
        if not mt:
            refuse("bad path data near %r" % d[pos:pos + 20])
        if mt.group(1):
            toks.append(mt.group(1))
        elif mt.group(2):
            toks.append(mt.group(2))
        pos = mt.end()
    out = []
    i = 0
    cmd = None
    x = y = sx = sy = 0.0
    lc = None  # last control point for S/T reflection
    lcmd = None

    def num():
        nonlocal i
        if i >= len(toks) or toks[i].isalpha():
            refuse("path data: missing number after %s" % cmd)
        v = float(toks[i])
        i += 1
        return v

    def flag():
        nonlocal i
        if i >= len(toks):
            refuse("path data: missing arc flag")
        t = toks[i]
        if t[0] in "01":
            if len(t) > 1:  # compact flags such as "11" or "1.5"
                toks[i] = t[1:]
            else:
                i += 1
            return t[0] == "1"
        refuse("path data: bad arc flag %r" % t)

    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]
            i += 1
        elif cmd is None:
            refuse("path data must start with a command")
        elif cmd in "Mm":
            cmd = "L" if cmd == "M" else "l"
        C = cmd.upper()
        rel = cmd.islower()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if C == "Z":
            out.append(("Z",))
            x, y = sx, sy
            lc = None
            lcmd = "Z"
            if i < len(toks) and not toks[i].isalpha():
                refuse("path data: numbers after Z")
            continue
        if C == "M":
            x, y = ox + num(), oy + num()
            sx, sy = x, y
            out.append(("M", x, y))
            lc = None
        elif C == "L":
            x, y = ox + num(), oy + num()
            out.append(("L", x, y))
            lc = None
        elif C == "H":
            x = ox + num()
            out.append(("L", x, y))
            lc = None
        elif C == "V":
            y = (y if rel else 0.0) + num()
            out.append(("L", x, y))
            lc = None
        elif C == "C":
            c1 = (ox + num(), oy + num())
            c2 = (ox + num(), oy + num())
            x, y = ox + num(), oy + num()
            out.append(("C", c1[0], c1[1], c2[0], c2[1], x, y))
            lc = c2
        elif C == "S":
            c1 = (2 * x - lc[0], 2 * y - lc[1]) if lc and lcmd in "CS" else (x, y)
            c2 = (ox + num(), oy + num())
            x, y = ox + num(), oy + num()
            out.append(("C", c1[0], c1[1], c2[0], c2[1], x, y))
            lc = c2
        elif C == "Q":
            c = (ox + num(), oy + num())
            x, y = ox + num(), oy + num()
            out.append(("Q", c[0], c[1], x, y))
            lc = c
        elif C == "T":
            c = (2 * x - lc[0], 2 * y - lc[1]) if lc and lcmd in "QT" else (x, y)
            x, y = ox + num(), oy + num()
            out.append(("Q", c[0], c[1], x, y))
            lc = c
        elif C == "A":
            rx, ry, rot = abs(num()), abs(num()), num()
            large, sweep = flag(), flag()
            x, y = ox + num(), oy + num()
            out.append(("A", rx, ry, rot, large, sweep, x, y))
            lc = None
        lcmd = C
    return out


def bbox_of(segs):
    """Geometric bounding box (curves sampled finely)."""
    pts = []
    cx = cy = 0.0
    sx = sy = 0.0
    for s in segs:
        if s[0] == "M":
            cx, cy = s[1], s[2]
            sx, sy = cx, cy
            pts.append((cx, cy))
        elif s[0] == "L":
            cx, cy = s[1], s[2]
            pts.append((cx, cy))
        elif s[0] == "Z":
            cx, cy = sx, sy
        elif s[0] == "Q":
            for k in range(1, 65):
                t = k / 64
                u = 1 - t
                pts.append((u * u * cx + 2 * u * t * s[1] + t * t * s[3],
                            u * u * cy + 2 * u * t * s[2] + t * t * s[4]))
            cx, cy = s[3], s[4]
        elif s[0] == "C":
            for k in range(1, 65):
                t = k / 64
                u = 1 - t
                pts.append((u ** 3 * cx + 3 * u * u * t * s[1] + 3 * u * t * t * s[3] + t ** 3 * s[5],
                            u ** 3 * cy + 3 * u * u * t * s[2] + 3 * u * t * t * s[4] + t ** 3 * s[6]))
            cx, cy = s[5], s[6]
        elif s[0] == "A":
            pts.extend(arc_points(cx, cy, s))
            cx, cy = s[6], s[7]
    if not pts:
        return (0.0, 0.0, 0.0, 0.0)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys))


def arc_points(x1, y1, s):
    _, rx, ry, rot, large, sweep, x2, y2 = s
    if rx == 0 or ry == 0 or (x1 == x2 and y1 == y2):
        return [(x2, y2)]
    phi = math.radians(rot)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    xp, yp = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (xp / rx) ** 2 + (yp / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp
    den = rx * rx * yp * yp + ry * ry * xp * xp
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * yp / ry, -co * ry * xp / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2
    t1 = math.atan2((yp - cyp) / ry, (xp - cxp) / rx)
    t2 = math.atan2((-yp - cyp) / ry, (-xp - cxp) / rx)
    dt = t2 - t1
    if sweep and dt < 0:
        dt += 2 * math.pi
    elif not sweep and dt > 0:
        dt -= 2 * math.pi
    out = []
    for k in range(1, 129):
        t = t1 + dt * k / 128
        out.append((cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp,
                    cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp))
    return out


# ---- the converter ---------------------------------------------------------
INHERITED = ("fill", "stroke", "stroke-width", "stroke-linejoin", "stroke-linecap",
             "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset", "fill-opacity",
             "stroke-opacity", "fill-rule", "color", "visibility")
REFUSED_ATTRS = ("filter", "mask", "clip-path", "marker-start", "marker-mid", "marker-end",
                 "marker")
IGNORED_TAGS = ("title", "desc", "metadata")
REFUSED_TAGS = ("text", "tspan", "textPath", "image", "filter", "mask", "clipPath", "pattern",
                "use", "symbol", "style", "foreignObject", "marker", "switch", "a", "script",
                "animate", "animateTransform", "animateMotion", "set")


def local(tag):
    if tag.startswith("{"):
        ns, t = tag[1:].split("}", 1)
        return ns, t
    return SVG_NS, tag


class Conv:
    def __init__(self, root):
        self.root = root
        self.pal = []
        self.grads = []
        self.ids = []
        self.cmd = bytearray()
        self.cx = self.cy = 0  # current point, q4
        self.depth = 0
        self.defs = {}
        self.stats = {"paths": 0, "groups": 0}
        for el in root.iter():
            i = el.get("id")
            if i is not None:
                self.defs[i] = el
        vb = root.get("viewBox")
        if vb is None:
            refuse("the <svg> has no viewBox (required: it fixes the drawing's coordinates)")
        v = [float(x) for x in re.findall(NUM, vb)]
        if len(v) != 4 or v[2] <= 0 or v[3] <= 0:
            refuse("bad viewBox %r" % vb)
        self.vb = v

    # -- tables
    def pal_index(self, rgba):
        if rgba not in self.pal:
            if len(self.pal) >= MAX_PAL:
                refuse("more than %d colours" % MAX_PAL)
            self.pal.append(rgba)
        return self.pal.index(rgba)

    def id_index(self, name):
        if not ID_RE.match(name):
            refuse("id %r: ids must be 1..31 chars of [A-Za-z0-9_.:-]" % name)
        if name in self.ids:
            refuse("duplicate id %r" % name)
        if len(self.ids) >= MAX_IDS:
            refuse("more than %d ids" % MAX_IDS)
        self.ids.append(name)
        return len(self.ids)

    # -- style
    def props(self, el, parent):
        p = {k: v for k, v in parent.items() if k in INHERITED}
        for k, v in el.attrib.items():
            ns, name = local(k)
            if ns == SVG_NS:
                p[name] = v
        st = el.get("style")
        if st:
            for decl in st.split(";"):
                if ":" in decl:
                    k, v = decl.split(":", 1)
                    p[k.strip()] = v.strip()
        for a in REFUSED_ATTRS:
            if a in p and p[a].strip() != "none":
                refuse("<%s>: %s is not supported" % (local(el.tag)[1], a))
        if p.get("mix-blend-mode", "normal") != "normal":
            refuse("mix-blend-mode is not supported")
        if p.get("vector-effect", "none") != "none":
            refuse("vector-effect is not supported")
        if p.get("paint-order", "normal") not in ("normal", "fill stroke", "fill"):
            refuse("paint-order is not supported")
        return p

    # -- emit
    def emit(self, op, *parts):
        self.cmd.append(op)
        for x in parts:
            self.cmd += x

    def group(self, name, opacity, m):
        if self.depth >= MAX_DEPTH:
            refuse("groups nested deeper than %d" % MAX_DEPTH)
        self.depth += 1
        self.stats["groups"] += 1
        idx = self.id_index(name) if name else 0
        op = max(0, min(255, int(round(opacity * 255))))
        body = uv(idx) + bytes([op])
        if m is not None and m != IDENT:
            body += bytes([1]) + b"".join(
                zz(fix(v, "transform value")) for v in m)
        else:
            body += bytes([0])
        self.emit(OP_GROUP, body)

    def endgroup(self):
        self.depth -= 1
        self.emit(OP_ENDGROUP)

    def pt(self, x, y):
        X, Y = q4(x), q4(y)
        d = zz(X - self.cx) + zz(Y - self.cy)
        return X, Y, d

    def emit_path(self, segs):
        self.emit(OP_NEWPATH)
        sx, sy = self.cx, self.cy
        for s in segs:
            if s[0] in "ML":
                X, Y, d = self.pt(s[1], s[2])
                self.emit(OP_MOVE if s[0] == "M" else OP_LINE, d)
                self.cx, self.cy = X, Y
                if s[0] == "M":
                    sx, sy = X, Y
            elif s[0] == "Z":
                self.emit(OP_CLOSE)
                self.cx, self.cy = sx, sy
            elif s[0] == "Q":
                c = zz(q4(s[1]) - self.cx) + zz(q4(s[2]) - self.cy)
                X, Y, d = self.pt(s[3], s[4])
                self.emit(OP_QUAD, c, d)
                self.cx, self.cy = X, Y
            elif s[0] == "C":
                c1 = zz(q4(s[1]) - self.cx) + zz(q4(s[2]) - self.cy)
                c2 = zz(q4(s[3]) - self.cx) + zz(q4(s[4]) - self.cy)
                X, Y, d = self.pt(s[5], s[6])
                self.emit(OP_CUBIC, c1, c2, d)
                self.cx, self.cy = X, Y
            elif s[0] == "A":
                _, rx, ry, rot, large, sweep, x, y = s
                rq, ryq = q4(rx), q4(ry)
                ang = int(round(rot / 360.0 * 65536)) % 65536
                X, Y, d = self.pt(x, y)
                self.emit(OP_ARC, uv(abs(rq)), uv(abs(ryq)), uv(ang),
                          bytes([(1 if large else 0) | (2 if sweep else 0)]), d)
                self.cx, self.cy = X, Y

    # -- gradients
    def gradient(self, gid, bbox, alpha):
        el = self.defs.get(gid)
        if el is None:
            refuse("paint refers to missing #%s" % gid)
        ns, tag = local(el.tag)
        if tag not in ("linearGradient", "radialGradient"):
            refuse("paint url(#%s) is a <%s>; only gradients are supported" % (gid, tag))
        # follow href chain for attributes and stops
        chain = []
        cur = el
        while cur is not None and len(chain) < 16:
            chain.append(cur)
            h = cur.get("{%s}href" % XLINK_NS) or cur.get("href")
            cur = self.defs.get(h[1:]) if h and h.startswith("#") else None

        def attr(name, default):
            for c in chain:
                if c.get(name) is not None:
                    return c.get(name)
            return default

        stops = []
        for c in chain:
            st = [s for s in c if local(s.tag)[1] == "stop"]
            if st:
                stops = st
                break
        if not stops:
            refuse("gradient #%s has no stops" % gid)
        if len(stops) > MAX_STOPS:
            refuse("gradient #%s has more than %d stops" % (gid, MAX_STOPS))
        units = attr("gradientUnits", "objectBoundingBox")
        obb = units == "objectBoundingBox"
        if units not in ("objectBoundingBox", "userSpaceOnUse"):
            refuse("bad gradientUnits %r" % units)
        vw, vh = self.vb[2], self.vb[3]

        def coord(v, ref):
            v = v.strip()
            if v.endswith("%"):
                f = float(v[:-1]) / 100
                return f if obb else f * ref
            return length(v, "gradient")

        rref = math.sqrt((vw * vw + vh * vh) / 2)
        if tag == "linearGradient":
            x1 = coord(attr("x1", "0%"), vw)
            y1 = coord(attr("y1", "0%"), vh)
            x2 = coord(attr("x2", "100%"), vw)
            y2 = coord(attr("y2", "0%"), vh)
            dx, dy = x2 - x1, y2 - y1
            unit = (dx, dy, -dy, dx, x1, y1)
            kind = 1
        else:
            cx = coord(attr("cx", "50%"), vw)
            cy = coord(attr("cy", "50%"), vh)
            r = coord(attr("r", "50%"), rref)
            fx = attr("fx", None)
            fy = attr("fy", None)
            if (fx is not None and abs(coord(fx, vw) - cx) > 1e-9) or \
               (fy is not None and abs(coord(fy, vh) - cy) > 1e-9):
                refuse("radialGradient #%s: a focal point off the centre is not supported" % gid)
            if attr("fr", "0") not in ("0", "0%"):
                refuse("radialGradient #%s: fr is not supported" % gid)
            unit = (r, 0.0, 0.0, r, cx, cy)
            kind = 2
        m = unit
        gt = attr("gradientTransform", None)
        if gt:
            m = mul(parse_transform(gt), m)
        if obb:
            bx, by, bw, bh = bbox
            if bw <= 0 or bh <= 0:
                return None  # SVG: an objectBoundingBox gradient on a zero-area box draws nothing
            m = mul((bw, 0, 0, bh, bx, by), m)
        spread = {"pad": 0, "reflect": 1, "repeat": 2}.get(attr("spreadMethod", "pad"))
        if spread is None:
            refuse("bad spreadMethod")
        recs = []
        last = 0
        for s in stops:
            p = self.props(s, {})
            off = number01(s.get("offset", "0"), "stop offset")
            o = max(last, int(round(off * 255)))
            last = o
            c = colour(p.get("stop-color", "black"), p.get("color"))
            if c is None:
                c = (0, 0, 0, 0.0)
            if isinstance(c[0], str):
                refuse("stop-color cannot be a url")
            a = c[3] * number01(p.get("stop-opacity", "1"), "stop-opacity")
            recs.append((o, self.pal_index((c[0], c[1], c[2], int(round(a * 255))))))
        rec = (kind, spread, tuple(fix(v, "gradient matrix value") for v in m), tuple(recs))
        if rec not in self.grads:
            if len(self.grads) >= MAX_GRAD:
                refuse("more than %d gradient uses" % MAX_GRAD)
            self.grads.append(rec)
        return (self.grads.index(rec) << 1) | 1

    def paint(self, p, key, opkey, segs, extra):
        v = p.get(key, "black" if key == "fill" else "none")
        c = colour(v, p.get("color"))
        if c is None:
            return None
        a = number01(p.get(opkey, "1"), opkey) * extra
        if isinstance(c[0], str):
            ref = self.gradient(c[1], bbox_of(segs), a)
            if ref is None:
                return None
            return uv(ref) + bytes([int(round(a * 255))])
        ref = self.pal_index((c[0], c[1], c[2], int(round(c[3] * 255))))
        return uv(ref << 1) + bytes([int(round(a * 255))])

    # -- shapes
    def shape(self, el, tag, p):
        def L(name, default=None):
            v = p.get(name)
            if v is None:
                if default is None:
                    refuse("<%s> needs %s" % (tag, name))
                return default
            return length(v, name)

        if tag == "path":
            return parse_d(p.get("d", ""))
        if tag == "rect":
            x, y, w, h = L("x", 0.0), L("y", 0.0), L("width"), L("height")
            if w <= 0 or h <= 0:
                return []
            rx, ry = p.get("rx"), p.get("ry")
            rx = None if rx in (None, "auto") else length(rx, "rx")
            ry = None if ry in (None, "auto") else length(ry, "ry")
            if rx is None:
                rx = ry
            if ry is None:
                ry = rx
            rx = min(abs(rx or 0.0), w / 2)
            ry = min(abs(ry or 0.0), h / 2)
            if rx <= 0 or ry <= 0:
                return [("M", x, y), ("L", x + w, y), ("L", x + w, y + h), ("L", x, y + h),
                        ("Z",)]
            return [("M", x + rx, y), ("L", x + w - rx, y),
                    ("A", rx, ry, 0, False, True, x + w, y + ry), ("L", x + w, y + h - ry),
                    ("A", rx, ry, 0, False, True, x + w - rx, y + h), ("L", x + rx, y + h),
                    ("A", rx, ry, 0, False, True, x, y + h - ry), ("L", x, y + ry),
                    ("A", rx, ry, 0, False, True, x + rx, y), ("Z",)]
        if tag in ("circle", "ellipse"):
            cx, cy = L("cx", 0.0), L("cy", 0.0)
            if tag == "circle":
                rx = ry = L("r")
            else:
                rx, ry = L("rx"), L("ry")
            if rx <= 0 or ry <= 0:
                return []
            return [("M", cx + rx, cy), ("A", rx, ry, 0, False, True, cx - rx, cy),
                    ("A", rx, ry, 0, False, True, cx + rx, cy), ("Z",)]
        if tag == "line":
            return [("M", L("x1", 0.0), L("y1", 0.0)), ("L", L("x2", 0.0), L("y2", 0.0))]
        if tag in ("polyline", "polygon"):
            n = [float(v) for v in re.findall(NUM, p.get("points", ""))]
            if len(n) < 4:
                return []
            if len(n) % 2:
                n = n[:-1]
            segs = [("M", n[0], n[1])] + [("L", n[i], n[i + 1]) for i in range(2, len(n), 2)]
            if tag == "polygon":
                segs.append(("Z",))
            return segs
        raise AssertionError(tag)

    def element(self, el, parent):
        ns, tag = local(el.tag)
        if ns != SVG_NS:
            sys.stderr.write("svg2zxvg: note: ignoring editor element <%s>\n" % tag)
            return
        if tag in IGNORED_TAGS:
            return
        if tag in REFUSED_TAGS:
            refuse("<%s> is not supported (convert it to paths first)" % tag)
        if tag in ("defs", "linearGradient", "radialGradient", "stop"):
            return  # referenced through url(#id)
        p = self.props(el, parent)
        if p.get("display", "inline") == "none":
            return
        tr = parse_transform(p["transform"]) if "transform" in p else IDENT
        opacity = number01(p.get("opacity", "1"), "opacity")
        name = el.get("id")
        if tag in ("g", "svg"):
            wrap = el is not self.root
            if wrap:
                self.group(name, opacity, tr)
            for ch in el:
                self.element(ch, p)
            if wrap:
                self.endgroup()
            return
        if tag not in ("path", "rect", "circle", "ellipse", "line", "polyline", "polygon"):
            refuse("<%s> is not supported" % tag)
        if p.get("visibility", "visible") in ("hidden", "collapse"):
            return
        segs = self.shape(el, tag, p)
        if not segs:
            return
        has_fill = colour(p.get("fill", "black"), p.get("color")) is not None and tag != "line"
        has_stroke = colour(p.get("stroke", "none"), p.get("color")) is not None
        sw = length(p.get("stroke-width", "1"), "stroke-width")
        if sw <= 0:
            has_stroke = False
        if not has_fill and not has_stroke:
            return
        both = has_fill and has_stroke
        wrap = name is not None or tr != IDENT or (both and opacity < 1)
        fold = 1.0 if (both and opacity < 1) else opacity
        if wrap:
            self.group(name, opacity if (both and opacity < 1) else 1.0, tr)
        self.stats["paths"] += 1
        self.emit_path(segs)
        if has_fill:
            pf = self.paint(p, "fill", "fill-opacity", segs, fold)
            if pf:
                rule = p.get("fill-rule", "nonzero").strip()
                if rule not in ("nonzero", "evenodd"):
                    refuse("bad fill-rule %r" % rule)
                self.emit(OP_FILL, bytes([1 if rule == "evenodd" else 0]), pf)
        if has_stroke:
            ps = self.paint(p, "stroke", "stroke-opacity", segs, fold)
            if ps:
                join = {"miter": 0, "round": 1, "bevel": 2, "miter-clip": 0,
                        "arcs": 1}.get(p.get("stroke-linejoin", "miter").strip())
                cap = {"butt": 0, "round": 1, "square": 2}.get(
                    p.get("stroke-linecap", "butt").strip())
                if join is None or cap is None:
                    refuse("bad stroke-linejoin / stroke-linecap")
                ml = float(p.get("stroke-miterlimit", "4"))
                if ml < 1:
                    refuse("stroke-miterlimit below 1")
                dash = []
                da = p.get("stroke-dasharray", "none").strip()
                if da != "none" and da != "":
                    dash = [length(v, "stroke-dasharray") for v in re.split(r"[\s,]+", da) if v]
                    if any(v < 0 for v in dash):
                        refuse("negative stroke-dasharray")
                    if len(dash) % 2:
                        dash = dash * 2
                    if sum(dash) <= 0:
                        dash = []
                    if len(dash) > MAX_DASH:
                        refuse("stroke-dasharray longer than %d" % MAX_DASH)
                off = length(p.get("stroke-dashoffset", "0"), "stroke-dashoffset")
                body = ps + uv(q4(sw)) + bytes([join, cap]) + uv(min(int(round(ml * 256)), 1 << 16))
                body += uv(len(dash)) + b"".join(uv(q4(v)) for v in dash) + zz(q4(off) if dash else 0)
                self.emit(OP_STROKE, body)
        if wrap:
            self.endgroup()

    def build(self):
        self.element(self.root, {})
        out = bytearray(b"ZXVG")
        out += bytes([VERSION, 0])
        out += zz(q4(self.vb[0])) + zz(q4(self.vb[1])) + uv(q4(self.vb[2])) + uv(q4(self.vb[3]))
        out += uv(len(self.pal))
        for r, g, b, a in self.pal:
            out += bytes([r, g, b, a])
        out += uv(len(self.grads))
        for kind, spread, m, stops in self.grads:
            out += bytes([kind, spread]) + b"".join(zz(v) for v in m)
            out += bytes([len(stops)])
            for o, pi in stops:
                out += bytes([o]) + uv(pi)
        out += uv(len(self.ids))
        for name in self.ids:
            b = name.encode("ascii")
            out += bytes([len(b)]) + b
        out += self.cmd
        out.append(OP_END)
        return bytes(out)


def convert(svg_bytes):
    try:
        root = ET.fromstring(svg_bytes)
    except ET.ParseError as e:
        refuse("not well-formed XML: %s" % e)
    if local(root.tag) != (SVG_NS, "svg"):
        refuse("the root element is not <svg>")
    if b"<!ENTITY" in svg_bytes:
        refuse("XML entities are not supported")
    c = Conv(root)
    return c.build(), c


def c_array(name, data, src):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        refuse("--c name must be a C identifier")
    lines = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
             "/* SPDX-License-Identifier: Apache-2.0 */",
             "/* Generated by tools/zxvg/svg2zxvg.py from %s; do not edit. */" % src,
             "/* ZXVG vector graphic, %d bytes. Draw with zui_vec_draw_zxvg(). */" % len(data),
             "static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 12]) + ",")
    lines.append("};")
    lines.append("static const uint32_t %s_len = %du;" % (name, len(data)))
    return "\n".join(lines) + "\n"


def main(argv):
    args = argv[1:]
    cname = cout = None
    if "--c" in args:
        i = args.index("--c")
        if len(args) < i + 3:
            sys.stderr.write("usage: svg2zxvg.py in.svg out.zxvg [--c name out.inc]\n")
            return 2
        cname, cout = args[i + 1], args[i + 2]
        del args[i:i + 3]
    if len(args) != 2:
        sys.stderr.write("usage: svg2zxvg.py in.svg out.zxvg [--c name out.inc]\n")
        return 2
    src, dst = args
    try:
        with open(src, "rb") as f:
            data, c = convert(f.read())
        inc = c_array(cname, data, src.replace("\\", "/").split("/")[-1]) if cname else None
    except Refuse as e:
        sys.stderr.write("svg2zxvg: %s: refused: %s\n" % (src, e))
        return 1
    with open(dst, "wb") as f:
        f.write(data)
    if inc is not None:
        with open(cout, "w") as f:
            f.write(inc)
    print("svg2zxvg: %s -> %s: %d bytes, %d paths, %d groups, %d colours, %d gradients, ids: %s"
          % (src, dst, len(data), c.stats["paths"], c.stats["groups"], len(c.pal), len(c.grads),
             ",".join(c.ids) or "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
