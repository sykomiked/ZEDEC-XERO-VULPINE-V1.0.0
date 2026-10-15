#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_icons.py: every app icon 001-407 is a vector, present once, and small.

zxv-brand/app-icons/svg/l1 holds 001-144, l2 145-271 (Dragon series), l3
272-407 (Crystal series), each NNN-name.svg. Fails on a missing, duplicated,
misplaced or oversized icon (over 30 KB), on a file that is not an SVG with
viewBox 0 0 1024 1024, and on an embedded raster (<image> or data: URI): the
icons are vectors, never pictures of vectors.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "zxv-brand", "app-icons", "svg")
TIERS = {"l1": (1, 144), "l2": (145, 271), "l3": (272, 407)}
MAX = 30 * 1024
NAME = re.compile(r"^(\d{3})-[a-z0-9-]+\.svg$")


def main():
    bad, seen = [], {}
    for tier, (lo, hi) in TIERS.items():
        d = os.path.join(ROOT, tier)
        if not os.path.isdir(d):
            bad.append("%s: missing directory" % tier)
            continue
        for f in sorted(os.listdir(d)):
            m = NAME.match(f)
            if not m:
                bad.append("%s/%s: not NNN-name.svg" % (tier, f))
                continue
            n = int(m.group(1))
            if not lo <= n <= hi:
                bad.append("%s/%s: %03d belongs outside %s" % (tier, f, n, tier))
            if n in seen:
                bad.append("%s/%s: %03d already in %s" % (tier, f, n, seen[n]))
            seen[n] = "%s/%s" % (tier, f)
            p = os.path.join(d, f)
            size = os.path.getsize(p)
            if size > MAX:
                bad.append("%s/%s: %d bytes, over %d" % (tier, f, size, MAX))
            head = open(p, "rb").read(4096).decode("utf-8", "replace")
            body = open(p, "rb").read().decode("utf-8", "replace")
            if not head.lstrip().startswith("<svg") or 'viewBox="0 0 1024 1024"' not in head:
                bad.append("%s/%s: not an SVG with viewBox 0 0 1024 1024" % (tier, f))
            if "<image" in body or "data:image" in body:
                bad.append("%s/%s: embeds a raster" % (tier, f))
    missing = [n for n in range(1, 408) if n not in seen]
    if missing:
        bad.append("missing icons: %s" % ", ".join("%03d" % n for n in missing[:20]))
    for b in bad:
        print("check_icons:", b)
    if bad:
        sys.exit(1)
    print("check_icons: 407 icons, all vector, all under %d bytes" % MAX)


if __name__ == "__main__":
    main()
