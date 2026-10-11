#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Check the relative links in the SDK docs.

Every Markdown file under sdk/docs/ (this script's grandparent folder) is
read, and every relative link in it must resolve:

  - [text](path) and [label]: path   the file or folder must exist;
  - [text](path#anchor)              the target Markdown file must have a
                                     heading (or an explicit <a id/name>)
                                     whose GitHub-style anchor is `anchor`;
  - [text](#anchor)                  the same, in the file itself.

Links that start with a scheme (https:, mailto:) are not fetched. Links
inside fenced code blocks and inline code spans are ignored.

Usage:
    python3 sdk/docs/guides/check_links.py [FILE_OR_DIR ...]

With no arguments it checks sdk/docs/**/*.md. Exit status: 0 when every
link resolves, 1 when any does not (each one is printed), 2 on a usage
error. Standard library only.
"""

from __future__ import annotations

import re
import sys
import unicodedata
from pathlib import Path
from typing import Dict, Iterator, List, Set, Tuple

DOCS = Path(__file__).resolve().parent.parent

FENCE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
HEADING = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
HTML_ANCHOR = re.compile(r"<a\s+[^>]*?(?:id|name)\s*=\s*[\"']([^\"']+)[\"']", re.IGNORECASE)
INLINE_CODE = re.compile(r"(`+)(?:(?!\1).)+?\1")
# [text](target "title") or ![alt](target); the text may hold nested brackets.
INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+[\"'][^\"']*[\"'])?\s*\)")
REF_DEF = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s+[\"'(].*)?$")
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


def strip_code(lines: List[str]) -> Iterator[Tuple[int, str]]:
    """Yield (line number, text) outside fenced code, with inline code removed."""
    fence = ""
    for no, line in enumerate(lines, 1):
        m = FENCE.match(line)
        if m:
            marker = m.group(1)
            if not fence:
                fence = marker[0] * len(marker)
                continue
            if marker[0] == fence[0] and len(marker) >= len(fence):
                fence = ""
                continue
        if fence:
            continue
        yield no, INLINE_CODE.sub("", line)


def slugify(heading: str) -> str:
    """GitHub's anchor for a heading text."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links keep their text
    text = re.sub(r"<[^>]+>", "", text)                          # inline HTML
    text = text.replace("`", "")
    text = text.strip().lower()
    out = []
    for ch in text:
        if ch == " ":
            out.append("-")
        elif ch in "-_" or unicodedata.category(ch)[0] in "LN" or unicodedata.category(ch) == "Mn":
            out.append(ch)
    return "".join(out)


_anchor_cache: Dict[Path, Set[str]] = {}


def anchors_of(path: Path) -> Set[str]:
    """Every anchor a Markdown file defines (headings, with -1, -2 for repeats)."""
    if path in _anchor_cache:
        return _anchor_cache[path]
    seen: Dict[str, int] = {}
    found: Set[str] = set()
    lines = path.read_text(encoding="utf-8").splitlines()
    fence = ""
    for line in lines:
        m = FENCE.match(line)
        if m:
            marker = m.group(1)
            if not fence:
                fence = marker[0] * len(marker)
                continue
            if marker[0] == fence[0] and len(marker) >= len(fence):
                fence = ""
                continue
        if fence:
            continue
        h = HEADING.match(line)
        if h:
            base = slugify(h.group(2))
            n = seen.get(base, 0)
            found.add(base if n == 0 else f"{base}-{n}")
            seen[base] = n + 1
        for a in HTML_ANCHOR.findall(line):
            found.add(a)
    _anchor_cache[path] = found
    return found


def links_of(path: Path) -> Iterator[Tuple[int, str]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    kept = list(strip_code(lines))
    for no, text in kept:
        r = REF_DEF.match(text)
        if r:
            yield no, r.group(1)
    # Link text may wrap onto the next line, so search the kept lines joined
    # together and map each match back to the line it starts on.
    joined = ""
    starts: List[Tuple[int, int]] = []
    for no, text in kept:
        starts.append((len(joined), no))
        joined += text + "\n"
    for m in INLINE_LINK.finditer(joined):
        no = next(n for off, n in reversed(starts) if off <= m.start())
        yield no, m.group(1)


def check_file(path: Path) -> List[str]:
    problems: List[str] = []
    for no, target in links_of(path):
        if SCHEME.match(target) or target.startswith("//"):
            continue
        file_part, _, anchor = target.partition("#")
        dest = path if not file_part else (path.parent / file_part).resolve()
        where = f"{path}:{no}: {target}"
        if not dest.exists():
            problems.append(f"{where}: no such file or folder")
            continue
        if anchor:
            if dest.is_dir() or dest.suffix.lower() != ".md":
                problems.append(f"{where}: an anchor can only point into a Markdown file")
            elif anchor not in anchors_of(dest):
                problems.append(f"{where}: no heading with anchor #{anchor} in {dest.name}")
    return problems


def collect(args: List[str]) -> List[Path]:
    roots = [Path(a) for a in args] if args else [DOCS]
    files: List[Path] = []
    for r in roots:
        if r.is_dir():
            files.extend(sorted(p for p in r.rglob("*.md") if p.is_file()))
        elif r.is_file():
            files.append(r)
        else:
            raise FileNotFoundError(str(r))
    return [f.resolve() for f in files]


def main(argv: List[str]) -> int:
    if any(a in ("-h", "--help") for a in argv):
        print(__doc__)
        return 0
    try:
        files = collect(argv)
    except FileNotFoundError as e:
        print(f"check_links: no such file or folder: {e}", file=sys.stderr)
        return 2
    problems: List[str] = []
    count = 0
    for f in files:
        count += sum(1 for _ in links_of(f))
        problems.extend(check_file(f))
    for p in problems:
        print(p)
    status = "FAIL" if problems else "OK"
    print(f"check_links: {status}: {len(files)} file(s), {count} link(s), {len(problems)} broken")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
