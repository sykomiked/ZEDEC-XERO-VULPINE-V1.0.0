#!/usr/bin/env python3
# gen_manifest.py — derive the seed-bank manifest from the committed tree.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Regenerate build/seedbank/manifest from the committed icon + font files,
so the seed bank is reproducible instead of hand-authored.

Order: the 408 vector glyphs (407 app icons + the OS mark, 408-zedec-
omnivault.zxvg) in numeric order, then web/boot/font.ttf, then every
font under assets/fonts/ascw-golden/ in name order.

usage: gen_manifest.py [repo-root] [out]
"""
import os
import sys


def icon_key(path):
    base = os.path.basename(path)
    num = base.split("-", 1)[0]
    return (0, int(num), base) if num.isdigit() else (1, 0, base)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "build", "seedbank", "manifest")

    icons = [os.path.join(root, "zxv-brand", "app-icons", "zxvg", f)
             for f in os.listdir(os.path.join(root, "zxv-brand", "app-icons", "zxvg"))
             if f.endswith(".zxvg")]
    if not icons:
        print("gen_manifest: no .zxvg glyphs found under zxv-brand/app-icons/zxvg", file=sys.stderr)
        return 1
    icons.sort(key=icon_key)

    fonts = [os.path.join(root, "web", "boot", "font.ttf")]
    ascw = os.path.join(root, "assets", "fonts", "ascw-golden")
    fonts += [os.path.join(ascw, f) for f in sorted(os.listdir(ascw)) if f.endswith(".ttf")]

    missing = [p for p in icons + fonts if not os.path.isfile(p)]
    if missing:
        for p in missing:
            print("gen_manifest: missing file: %s" % p, file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("# the inaugural ZXV seed bank: the %d icons (407 apps + the OS mark) + the fonts\n"
                % len(icons))
        for p in icons + fonts:
            f.write(p + "\n")
    print("gen_manifest: %d icons + %d fonts -> %s" % (len(icons), len(fonts), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())