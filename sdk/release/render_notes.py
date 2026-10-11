#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Render the draft release notes from NOTES_TEMPLATE.md.

  python3 sdk/release/render_notes.py --version 1.3.0 --tag sdk-v1.3.0 \\
      --commit <sha> --dist dist --prerelease false --out notes.md

Placeholders: {{VERSION}} {{CORE_VERSION}} {{TAG}} {{COMMIT}} {{DATE}}
{{CHANNEL}} {{GO_TAG}} {{ASSETS}}. {{ASSETS}} becomes a table of every file
in --dist (name, size); the hashes are in SHA256SUMS. Any placeholder left
unfilled is an error, so the template and this script cannot drift apart
silently. Standard library only.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def human(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return str(n)


def assets_table(dist: Path) -> str:
    files = sorted(p for p in dist.iterdir() if p.is_file())
    if not files:
        return "_No files were attached._"
    rows = ["| File | Size |", "|---|---|"]
    rows += [f"| `{p.name}` | {human(p.stat().st_size)} |" for p in files]
    return "\n".join(rows)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", default=str(HERE / "NOTES_TEMPLATE.md"))
    ap.add_argument("--version", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--commit", required=True)
    ap.add_argument("--dist", required=True, help="directory of release assets")
    ap.add_argument("--prerelease", choices=("true", "false"), required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)

    core = re.match(r"^\d+\.\d+\.\d+", a.version)
    if not core:
        print(f"error: bad version {a.version!r}", file=sys.stderr)
        return 2
    values = {
        "VERSION": a.version,
        "CORE_VERSION": core.group(0),
        "TAG": a.tag,
        "COMMIT": a.commit,
        "DATE": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d"),
        "CHANNEL": "pre-release" if a.prerelease == "true" else "release",
        "GO_TAG": f"sdk/go/zxv/v{a.version}",
        "ASSETS": assets_table(Path(a.dist)),
    }
    text = Path(a.template).read_text(encoding="utf-8")
    # The template's own licence header is for the repository, not the notes.
    text = re.sub(r"\A(?:<!--.*?-->\s*)+", "", text, flags=re.S)
    text = re.sub(r"\{\{([A-Z_]+)\}\}", lambda m: values.get(m.group(1), m.group(0)), text)
    left = sorted(set(re.findall(r"\{\{[A-Z_]+\}\}", text)))
    if left:
        print(f"error: unfilled placeholders in {a.template}: {', '.join(left)}", file=sys.stderr)
        return 1
    Path(a.out).write_text(text, encoding="utf-8")
    print(f"wrote {a.out} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
