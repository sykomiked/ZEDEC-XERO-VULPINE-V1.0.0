#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""lint_icons.py — the build-time vector linter for app icon SVGs.

Every app icon ships as a vector drawing that the kernel's integer renderer
(zui_vec, ZXVG) draws and the kinetic engine (kernel/src/zui/zui_kinetic.h)
animates by group. This linter refuses anything that would not survive that
path. Each rule is a named check, reported per file:

  LI1 parse          The file is well-formed XML with an <svg> root and no
                     DOCTYPE or entity declarations.
  LI2 viewbox        The root viewBox is exactly "0 0 1024 1024".
  LI3 no-raster      No <image>, <foreignObject>, <filter> (or fe*), <mask>,
                     <text>/<tspan>/<textPath>, no filter= or mask= (attribute
                     or style), no data: URI anywhere, and no href/xlink:href
                     to a raster file or to anything outside the document
                     (only "#id" references are allowed).
  LI4 precision      Every number in geometry (d, points, x, y, width,
                     height, cx, cy, r, rx, ry, x1..y2, fx, fy, offset,
                     transform, stroke-width, viewBox) is an integer or has at
                     most 2 decimal places, written without an exponent.
  LI5 vertex-budget  At most 400 (--max-knots) knots in the whole drawing.
                     A knot is an ON-CURVE point: each path segment's end
                     point (M, L, H, V, C, S, Q, T, A, absolute or relative,
                     implicit repeats included) counts 1; Z counts 0; Bezier
                     control points are NOT counted (they are reported as
                     "controls" for information). Shapes: rect 4 (8 when
                     rounded), circle 4, ellipse 4, line 2, polygon and
                     polyline one per point. Shapes inside <defs>/<symbol>
                     count where a <use> draws them (each time).
  LI6 stroke-width   No stroke narrower than 8 units at 1024: wherever the
                     effective stroke (inherited, attribute or style) is not
                     "none", the effective stroke-width (default 1) times the
                     scale of the accumulated transforms (sqrt|det|) must be
                     >= 8. Strokes are meant to be expanded to fills; a
                     stroke with no width is 1 and fails.
  LI7 groups         The root holds exactly three addressable groups, <g
                     id="chassis">, <g id="core-artifact"> and <g
                     id="kinetic-element">, each once, and no other drawable
                     element (title, desc, metadata and defs may sit beside
                     them). Nested ids inside the groups are free, but the
                     three ids are not reused anywhere else.

Content inside <metadata> (provenance manifests and the like) is not drawn
and is skipped by every check.

Usage:
  python3 tools/zxvg/lint_icons.py PATH... [--json] [--quiet] [--max-knots N]

PATH is a file or a directory (searched recursively for *.svg, following
symlinks). Exit 0 when every file passes, 1 when any check fails, 2 when a
path is missing or holds no SVG. Python stdlib only.
"""
import argparse
import json
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

CHECKS = ("parse", "viewbox", "no-raster", "precision", "vertex-budget", "stroke-width",
          "groups")
MAX_KNOTS = 400
MIN_STROKE = 8.0
GROUP_IDS = ("chassis", "core-artifact", "kinetic-element")
MAX_MESSAGES = 8  # per check per file; the rest is counted

FORBIDDEN = {"image", "foreignObject", "filter", "mask", "text", "tspan", "textPath"}
RASTER_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".tif", ".tiff", ".ico",
              ".avif", ".heic", ".heif", ".jxl")
NUMERIC_ATTRS = {"d", "points", "x", "y", "width", "height", "cx", "cy", "r", "rx", "ry",
                 "x1", "y1", "x2", "y2", "fx", "fy", "offset", "transform", "stroke-width",
                 "viewBox"}
NON_RENDERED_TOP = {"title", "desc", "metadata", "defs"}
SHAPES = {"path", "rect", "circle", "ellipse", "line", "polygon", "polyline"}

NUM_RE = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
PATH_PARAMS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7, "Z": 0}
# control points per segment (two numbers each)
PATH_CONTROLS = {"C": 2, "S": 1, "Q": 1}


def local(tag):
    """The element or attribute name without its namespace."""
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


def style_props(el):
    """The style="" declarations of an element as a dict."""
    out = {}
    for decl in (el.get("style") or "").split(";"):
        if ":" in decl:
            k, v = decl.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def prop(el, name):
    """A presentation property: style wins over the attribute."""
    s = style_props(el)
    if name in s:
        return s[name]
    return el.get(name)


# ---- path parsing ---------------------------------------------------------


class PathError(ValueError):
    pass


def parse_path(d):
    """Count a path's knots. Returns (on_curve, controls)."""
    i, n = 0, len(d)
    cmd = None
    on, ctl = 0, 0

    def skip(i):
        while i < n and (d[i].isspace() or d[i] == ","):
            i += 1
        return i

    def number(i):
        m = NUM_RE.match(d, i)
        if not m:
            raise PathError("expected a number at %d in path data" % i)
        return m.end()

    def flag(i):
        if i < n and d[i] in "01":
            return i + 1
        raise PathError("expected an arc flag at %d in path data" % i)

    while True:
        i = skip(i)
        if i >= n:
            break
        c = d[i]
        if c.isalpha():
            up = c.upper()
            if up not in PATH_PARAMS:
                raise PathError("unknown path command %r" % c)
            if cmd is None and up != "M":
                raise PathError("path data must start with M")
            cmd = up
            i += 1
            if cmd == "Z":
                continue
            i = skip(i)
        elif cmd is None:
            raise PathError("path data must start with a command")
        elif cmd == "Z":
            raise PathError("numbers after Z")
        if cmd == "Z":
            continue
        # one parameter set
        for k in range(PATH_PARAMS[cmd]):
            i = skip(i)
            i = flag(i) if cmd == "A" and k in (3, 4) else number(i)
        on += 1
        ctl += PATH_CONTROLS.get(cmd, 0)  # implicit repeats (M x y x y = M L) count too
    return on, ctl


def shape_knots(el):
    """(on_curve, controls) for one shape element."""
    name = local(el.tag)
    if name == "path":
        return parse_path(el.get("d") or "")
    if name == "rect":
        rounded = any(_num(el.get(a)) for a in ("rx", "ry"))
        return (8 if rounded else 4), 0
    if name in ("circle", "ellipse"):
        return 4, 0
    if name == "line":
        return 2, 0
    if name in ("polygon", "polyline"):
        nums = NUM_RE.findall(el.get("points") or "")
        return len(nums) // 2, 0
    return 0, 0


def _num(v):
    if not v:
        return 0.0
    m = NUM_RE.match(v.strip())
    return float(m.group(0)) if m else 0.0


# ---- transforms -----------------------------------------------------------

TRANSFORM_RE = re.compile(r"(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\)")


def transform_det(t):
    """|det| of an SVG transform list (the area scale)."""
    if not t:
        return 1.0
    det = 1.0
    for name, args in TRANSFORM_RE.findall(t):
        a = [float(x) for x in NUM_RE.findall(args)]
        if name == "matrix" and len(a) == 6:
            det *= a[0] * a[3] - a[1] * a[2]
        elif name == "scale" and a:
            det *= a[0] * (a[1] if len(a) > 1 else a[0])
    return abs(det)


# ---- the checks -----------------------------------------------------------


class Report:
    def __init__(self, path):
        self.path = path
        self.checks = {c: [] for c in CHECKS}  # the first MAX_MESSAGES messages
        self.counts = {c: 0 for c in CHECKS}
        self.knots = 0
        self.controls = 0

    def fail(self, check, msg):
        self.counts[check] += 1
        if len(self.checks[check]) < MAX_MESSAGES:
            self.checks[check].append(msg)

    @property
    def ok(self):
        return not any(self.counts.values())

    def to_json(self):
        out = {}
        for c in CHECKS:
            out[c] = {"ok": not self.counts[c], "count": self.counts[c],
                      "messages": self.checks[c]}
        return {"path": self.path, "ok": self.ok, "knots": self.knots,
                "controls": self.controls, "checks": out}


def walk(el, parents=()):
    """Yield (element, ancestors) for the drawn tree, skipping <metadata>."""
    yield el, parents
    if local(el.tag) == "metadata":
        return
    for ch in el:
        if not isinstance(ch.tag, str):
            continue
        yield from walk(ch, parents + (el,))


def check_viewbox(root, rep):
    vb = root.get("viewBox")
    if vb != "0 0 1024 1024":
        rep.fail("viewbox", "root viewBox is %r, not \"0 0 1024 1024\"" % vb)


def check_raster(root, rep):
    for el, _ in walk(root):
        name = local(el.tag)
        if name == "metadata":
            continue
        if name in FORBIDDEN or name.startswith("fe"):
            rep.fail("no-raster", "<%s> is not allowed" % name)
        for k, v in el.attrib.items():
            an = local(k)
            if "data:" in v.replace(" ", "").lower():
                rep.fail("no-raster", "data: URI in %s on <%s>" % (an, name))
            if an == "href":
                ref = v.strip()
                low = ref.lower().split("?")[0].split("#")[0]
                if low.endswith(RASTER_EXT):
                    rep.fail("no-raster", "href to a raster file %r on <%s>" % (ref, name))
                elif not ref.startswith("#"):
                    rep.fail("no-raster", "href outside the document %r on <%s>" % (ref, name))
        for p in ("filter", "mask"):
            v = prop(el, p)
            if v and v.strip() != "none":
                rep.fail("no-raster", "%s=%r on <%s>" % (p, v, name))
        if el.text and "data:" in el.text.replace(" ", "").lower():
            rep.fail("no-raster", "data: URI in the text of <%s>" % name)


def check_precision(root, rep):
    for el, _ in walk(root):
        if local(el.tag) == "metadata":
            continue
        for k, v in el.attrib.items():
            an = local(k)
            if an not in NUMERIC_ATTRS:
                continue
            for tok in NUM_RE.findall(v):
                exp = "e" in tok.lower()
                dec = len(tok.split(".", 1)[1].split("e")[0].split("E")[0]) if "." in tok else 0
                if exp or dec > 2:
                    rep.fail("precision", "%s on <%s>: %s" % (an, local(el.tag), tok))


def check_knots(root, rep, max_knots):
    ids = {el.get("id"): el for el, _ in walk(root) if el.get("id")}
    on_total, ctl_total = 0, 0

    def count(el, depth, in_defs):
        nonlocal on_total, ctl_total
        name = local(el.tag)
        if name == "metadata" or depth > 32:
            return
        in_defs = in_defs or name in ("defs", "symbol")
        if name in SHAPES and not in_defs:
            try:
                on, ctl = shape_knots(el)
            except PathError as e:
                rep.fail("vertex-budget", "bad path data: %s" % e)
                on, ctl = 0, 0
            on_total += on
            ctl_total += ctl
        if name == "use" and not in_defs:
            ref = None
            for k, v in el.attrib.items():
                if local(k) == "href" and v.startswith("#"):
                    ref = ids.get(v[1:])
            if ref is not None:
                count(ref, depth + 1, False)
        for ch in el:
            if isinstance(ch.tag, str):
                count(ch, depth + 1, in_defs)

    count(root, 0, False)
    rep.knots, rep.controls = on_total, ctl_total
    if on_total > max_knots:
        rep.fail("vertex-budget", "%d knots (on-curve points), budget %d" % (on_total, max_knots))


def check_strokes(root, rep):
    def visit(el, stroke, width, det):
        name = local(el.tag)
        if name == "metadata":
            return
        s = prop(el, "stroke")
        if s is not None and s.strip() != "inherit":
            stroke = s.strip()
        w = prop(el, "stroke-width")
        if w is not None and w.strip() != "inherit":
            width = w.strip()
        det *= transform_det(el.get("transform"))
        if name in SHAPES and stroke and stroke != "none":
            m = NUM_RE.fullmatch(width.replace("px", "").strip()) if width else None
            if not m:
                rep.fail("stroke-width", "<%s> stroke-width %r is not a plain number" %
                         (name, width))
            else:
                eff = float(m.group(0)) * math.sqrt(det)
                if eff < MIN_STROKE:
                    rep.fail("stroke-width", "<%s%s> stroke %s width %g (< %g); expand to a fill" %
                             (name, (" id=%s" % el.get("id")) if el.get("id") else "", stroke,
                              round(eff, 2), MIN_STROKE))
        for ch in el:
            if isinstance(ch.tag, str):
                visit(ch, stroke, width, det)

    visit(root, None, "1", 1.0)


def check_groups(root, rep):
    seen = []
    for ch in root:
        if not isinstance(ch.tag, str):
            continue
        name = local(ch.tag)
        if name in NON_RENDERED_TOP:
            continue
        gid = ch.get("id")
        if name == "g" and gid in GROUP_IDS:
            seen.append(gid)
        else:
            rep.fail("groups", "top-level <%s%s> outside the three groups" %
                     (name, (" id=%s" % gid) if gid else ""))
    for gid in GROUP_IDS:
        c = seen.count(gid)
        if c != 1:
            rep.fail("groups", "top-level <g id=%s> appears %d times (want 1)" % (gid, c))
    for el, parents in walk(root):
        if el.get("id") in GROUP_IDS and len(parents) != 1:
            rep.fail("groups", "id %s reused on a nested <%s>" % (el.get("id"), local(el.tag)))


def lint_text(text, path="<string>", max_knots=MAX_KNOTS):
    rep = Report(path)
    if re.search(r"<!DOCTYPE|<!ENTITY", text, re.I):
        rep.fail("parse", "DOCTYPE or entity declarations are not allowed")
        return rep
    try:
        root = ET.fromstring(text)
    except ET.ParseError as e:
        rep.fail("parse", "not well-formed XML: %s" % e)
        return rep
    if local(root.tag) != "svg":
        rep.fail("parse", "root element is <%s>, not <svg>" % local(root.tag))
        return rep
    check_viewbox(root, rep)
    check_raster(root, rep)
    check_precision(root, rep)
    check_knots(root, rep, max_knots)
    check_strokes(root, rep)
    check_groups(root, rep)
    return rep


def lint_file(path, max_knots=MAX_KNOTS):
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except (OSError, UnicodeDecodeError) as e:
        rep = Report(path)
        rep.fail("parse", "cannot read: %s" % e)
        return rep
    return lint_text(text, path, max_knots)


def find_svgs(paths):
    out, missing = [], []
    for p in paths:
        if os.path.isdir(p):
            for dirpath, dirnames, filenames in os.walk(p, followlinks=True):
                dirnames.sort()
                for fn in sorted(filenames):
                    if fn.lower().endswith(".svg"):
                        out.append(os.path.join(dirpath, fn))
        elif os.path.isfile(p):
            out.append(p)
        else:
            missing.append(p)
    return out, missing


def summarize(reports):
    per = {c: sum(1 for r in reports if r.counts[c]) for c in CHECKS}
    return {"files": len(reports), "passed": sum(1 for r in reports if r.ok),
            "failed": sum(1 for r in reports if not r.ok), "failures_by_check": per,
            "max_knots_seen": max((r.knots for r in reports), default=0)}


def main(argv=None):
    ap = argparse.ArgumentParser(description="Lint app icon SVGs for the vector pipeline.")
    ap.add_argument("paths", nargs="+", help="SVG files or directories")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--quiet", action="store_true", help="only failures and the summary")
    ap.add_argument("--max-knots", type=int, default=MAX_KNOTS)
    a = ap.parse_args(argv)
    files, missing = find_svgs(a.paths)
    for m in missing:
        print("lint_icons: no such file or directory: %s" % m, file=sys.stderr)
    if missing or not files:
        if not files:
            print("lint_icons: no SVG files found", file=sys.stderr)
        return 2
    reports = [lint_file(f, a.max_knots) for f in files]
    summary = summarize(reports)
    if a.json:
        json.dump({"files": [r.to_json() for r in reports], "summary": summary}, sys.stdout,
                  indent=1)
        sys.stdout.write("\n")
    else:
        for r in reports:
            if r.ok:
                if not a.quiet:
                    print("PASS %s (%d knots)" % (r.path, r.knots))
                continue
            print("FAIL %s (%d knots)" % (r.path, r.knots))
            for c in CHECKS:
                for m in r.checks[c]:
                    print("  [%s] %s" % (c, m))
                if r.counts[c] > len(r.checks[c]):
                    print("  [%s] ... and %d more" % (c, r.counts[c] - len(r.checks[c])))
        print("%d files: %d passed, %d failed" % (summary["files"], summary["passed"],
                                                  summary["failed"]))
        for c in CHECKS:
            if summary["failures_by_check"][c]:
                print("  %-14s failed in %d files" % (c, summary["failures_by_check"][c]))
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
