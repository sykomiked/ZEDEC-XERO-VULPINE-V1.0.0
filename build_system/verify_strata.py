#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""verify_strata.py: the seven-strata layering check (gap B1).

Every kernel module belongs to one stratum, and a lower stratum never depends
on a higher one (rule R2). The strata are the ones the architecture document
uses, with the shared base below them and the products above them:

    0 base        pure building blocks every stratum may use
    1 kernel      kernel and hardware abstraction
    2 transport   frames, mesh, DHT, wire
    3 trust       post-quantum crypto, identity, capabilities
    4 logic       paraconsistent logic, proofs, fault handling, forecasting
    5 ai          integer AI runtime and the swarm
    6 settlement  ledgers, payments, markets, the economy
    7 adapters    jurisdiction and format adapters, the five language pillars
    8 above       products, apps and people features

The assignment lives in build_system/strata.txt, one line per directory or file
under kernel/src. The longest matching path wins, so a file can be pinned apart
from its directory; a path without an extension pins the .c and .h together.

The check fails (exit 1) on any of:
  UNASSIGNED  a kernel/src source or header that no map line covers
  STALE       a map line that matches nothing in the tree
  STRATUM     a file that includes a header owned by a higher stratum
  REQUIRES    a ZXV_REQUIRES capability that nothing in kernel/ PROVIDES

Usage:
  build_system/verify_strata.py [--root DIR] [--map FILE] [--quiet]
  build_system/verify_strata.py --graph     print the module include graph
"""
import argparse
import os
import re
import sys

STRATA = ["base", "kernel", "transport", "trust", "logic", "ai",
          "settlement", "adapters", "above"]

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
NEGTEST = "ZXV_DECL_NEGTEST"
DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define(?:[^\n]*\\\n)*[^\n]*", re.M)
CAP_RE = re.compile(
    r"ZXV_(PROVIDES|REQUIRES)(_AT|_PHASED)?[ \t]*\(([^()]*)\)")


def skip_source(name):
    """Tests and debug drivers are not modules."""
    return name.startswith("test_") or name.startswith("debug_")


def load_map(path):
    """Read the map. Returns ({relpath: stratum}, [errors])."""
    entries = {}
    errors = []
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            text = line.split("#", 1)[0].strip()
            if not text:
                continue
            parts = text.split()
            if len(parts) != 2:
                errors.append("%s:%d: expected '<path> <stratum>'" % (path, n))
                continue
            key, name = parts[0].strip("/"), parts[1]
            if name not in STRATA:
                errors.append("%s:%d: unknown stratum %s" % (path, n, name))
                continue
            if key in entries:
                errors.append("%s:%d: %s mapped twice" % (path, n, key))
                continue
            entries[key] = STRATA.index(name)
    return entries, errors


def stratum_of(rel, entries):
    """Longest map path that is rel itself, rel without its extension (one
    line covers foo.c and foo.h), or a directory above it."""
    stem = os.path.splitext(rel)[0]
    if stem in entries:
        return entries[stem], stem
    probe = rel
    while probe:
        if probe in entries:
            return entries[probe], probe
        if "/" not in probe:
            break
        probe = probe.rsplit("/", 1)[0]
    return None, None


def walk(top, suffixes):
    """Sorted relative paths of files under top with one of the suffixes."""
    found = []
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames.sort()
        for f in filenames:
            if f.endswith(suffixes):
                found.append(os.path.relpath(os.path.join(dirpath, f), top))
    found.sort()
    return found


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


class Tree:
    def __init__(self, root):
        self.root = root
        self.src = os.path.join(root, "kernel", "src")
        self.inc = os.path.join(root, "kernel", "include")
        self.headers = walk(self.src, (".h",))
        self.by_base = {}
        for h in self.headers:
            self.by_base.setdefault(os.path.basename(h), []).append(h)
        self.header_set = set(self.headers)

    def resolve(self, includer, inc):
        """The kernel/src header an include names, or None when the header
        lives outside kernel/src (kernel/include, arch, third_party) or is
        not in the tree at all. Order follows the compiler: the includer's own
        directory, then kernel/include, then kernel/src, then a header of that
        name anywhere in kernel/src (the -I directories), preferring one in the
        includer's own top-level directory."""
        here = os.path.dirname(includer)
        rel = os.path.normpath(os.path.join(here, inc))
        if rel in self.header_set:
            return rel
        if os.path.exists(os.path.normpath(os.path.join(self.src, here, inc))):
            return None
        if os.path.exists(os.path.join(self.inc, inc)):
            return None
        rel = os.path.normpath(inc)
        if rel in self.header_set:
            return rel
        cands = self.by_base.get(os.path.basename(inc), [])
        cands = [c for c in cands if c.endswith(os.path.normpath(inc))]
        if not cands:
            return None
        top = includer.split("/", 1)[0]
        own = [c for c in cands if c.split("/", 1)[0] == top]
        return (own or cands)[0]


def drop_negtests(text):
    """Remove #if blocks guarded by ZXV_DECL_NEGTEST: those are the deliberate
    unprovided/cycle fixtures for the boot gate, off in every real build."""
    out, depth = [], 0
    for line in text.split("\n"):
        d = line.strip()
        if depth:
            if re.match(r"#\s*if", d):
                depth += 1
            elif re.match(r"#\s*endif", d):
                depth -= 1
            continue
        if re.match(r"#\s*if", d) and NEGTEST in d:
            depth = 1
            continue
        out.append(line)
    return "\n".join(out)


def caps(text):
    """[(kind, capability)] declared in a source text."""
    out = []
    text = DEFINE_RE.sub(" ", drop_negtests(strip_comments(text)))
    for kind, form, args in CAP_RE.findall(text):
        names = [a.strip() for a in args.split(",")]
        if form:
            names = names[:1]
        out.extend((kind, n) for n in names if re.match(r"^[A-Za-z_]\w*$", n))
    return out


def check(root, map_path, graph=False, quiet=False):
    entries, errors = load_map(map_path)
    tree = Tree(root)
    sources = [s for s in walk(tree.src, (".c",))
               if not skip_source(os.path.basename(s))]
    used = set()
    unassigned = []
    level = {}
    for f in sources + tree.headers:
        s, key = stratum_of(f, entries)
        if s is None:
            unassigned.append(f)
            continue
        level[f] = s
        used.add(key)
    stale = sorted(k for k in entries if k not in used)

    violations = []
    edges = {}
    for f in sources + tree.headers:
        for inc in INCLUDE_RE.findall(strip_comments(read(
                os.path.join(tree.src, f)))):
            h = tree.resolve(f, inc)
            if h is None:
                continue
            a, b = f.split("/", 1)[0], h.split("/", 1)[0]
            if a != b:
                edges.setdefault(a, set()).add(b)
            if f in level and h in level and level[h] > level[f]:
                violations.append((f, level[f], h, level[h]))

    provided, required = set(), {}
    for top in ("kernel",):
        for f in walk(os.path.join(root, top), (".c", ".h")):
            if skip_source(os.path.basename(f)) or "third_party" in f:
                continue
            for kind, cap in caps(read(os.path.join(root, top, f))):
                if kind == "PROVIDES":
                    provided.add(cap)
                else:
                    required.setdefault(cap, []).append("kernel/" + f)
    unresolved = sorted((c, sorted(set(w))) for c, w in required.items()
                        if c not in provided)

    if graph:
        for a in sorted(edges):
            print("%s -> %s" % (a, " ".join(sorted(edges[a]))))
        return 0

    for e in errors:
        print("  MAP       " + e)
    for f in unassigned:
        print("  UNASSIGNED  kernel/src/%s has no stratum in %s"
              % (f, os.path.relpath(map_path, root)))
    for k in stale:
        print("  STALE       %s matches nothing under kernel/src" % k)
    for f, a, h, b in violations:
        print("  STRATUM     kernel/src/%s (S%d %s) includes kernel/src/%s "
              "(S%d %s)" % (f, a, STRATA[a], h, b, STRATA[b]))
    for c, who in unresolved:
        print("  REQUIRES    %s is required by %s and provided by nothing"
              % (c, ", ".join(who)))
    bad = len(errors) + len(unassigned) + len(stale) + len(violations) + \
        len(unresolved)
    if not quiet:
        print("  strata: %d sources, %d headers, %d map lines; %d "
              "capabilities required, %d provided"
              % (len(sources), len(tree.headers), len(entries),
                 len(required), len(provided)))
    if bad:
        print("FAIL: %d strata problem(s)." % bad)
        return 1
    if not quiet:
        print("PASS: every module has a stratum, no stratum depends upward, "
              "every REQUIRES is provided.")
    return 0


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--map", default=None)
    ap.add_argument("--graph", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    m = a.map or os.path.join(a.root, "build_system", "strata.txt")
    return check(a.root, m, graph=a.graph, quiet=a.quiet)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
