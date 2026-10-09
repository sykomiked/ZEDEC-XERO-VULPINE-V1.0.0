#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Rewrite ZXV's old four-instrument SPDX expression to Apache-2.0.

Run from the repository root. Idempotent: running it twice changes nothing
the second time. Vendored third-party code (any directory that carries its
own LICENSE or README.zxv) is skipped, and so is every SPDX expression that
is not one of the two old ZXV stack expressions.

  build_system/relicense_apache.py          rewrite in place
  build_system/relicense_apache.py --check  exit 1 if any file still has it
"""
import os
import re
import subprocess
import sys

OLD = (
    "LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND "
    "LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3",
    "LicenseRef-OPL-1.1 AND CC-BY-SA-4.0",
)
NEW = "Apache-2.0"
# The prose line pair under the old SPDX header, in any comment style.
PROSE = re.compile(
    r"^(?P<p>[ \t]*(?:\*|#|//|;)?[ \t]*)Licensed under OPL-1\.1, SEL-3\.3, "
    r"the Royal Writ of the Sicilian Crown,\n"
    r"[ \t]*(?:\*|#|//|;)?[ \t]*and CC BY-SA 4\.0\. See LICENSE at the "
    r"repository root\.", re.M)
PROSE_NEW = r"\g<p>Licensed under the Apache License, Version 2.0. See LICENSE at\n" \
            r"\g<p>the repository root."
# Boot and --version banners.
BANNER = re.compile(r'"License: (?:SEL-3\.3[^"\\]*|OPL-1\.1[^"\\]*)')
BANNER_NEW = '"License: Apache-2.0'
# "License: SEL-3.3 ..." comment lines in sources, scripts and linker files.
COMMENT = re.compile(
    r"^(?P<p>[ \t]*(?:\*|#|//|;)?[ \t]*)License: (?:SEL-3\.3|OPL)[^\n]*$", re.M)
SELF = os.path.join("build_system", "relicense_apache.py")


def repo_files():
    out = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        check=True, capture_output=True).stdout
    return sorted({p for p in out.decode().split("\0") if p})


def vendored(path, cache={}):
    d = os.path.dirname(path)
    while d:
        if d not in cache:
            cache[d] = any(os.path.isfile(os.path.join(d, n))
                           for n in ("LICENSE", "README.zxv"))
        if cache[d]:
            return True
        d = os.path.dirname(d)
    return False


def main():
    check = "--check" in sys.argv[1:]
    changed, skipped, left = [], [], []
    for path in repo_files():
        if path == SELF or not os.path.isfile(path) or os.path.islink(path):
            continue
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            continue
        if not (b"LicenseRef-OPL-1.1" in data or b"Royal Writ of the Sicilian Crown," in data
                or b"License: SEL-3.3" in data or b"License: OPL" in data):
            continue
        if vendored(path):
            skipped.append(path)
            continue
        text = data.decode("utf-8", errors="surrogateescape")
        new = text
        for old in OLD:  # longest expression first
            new = new.replace(old, NEW)
        new = PROSE.sub(PROSE_NEW, new)
        new = BANNER.sub(BANNER_NEW, new)
        new = COMMENT.sub(r"\g<p>License: Apache-2.0", new)
        if new == text:
            continue
        if check:
            left.append(path)
            continue
        with open(path, "wb") as f:
            f.write(new.encode("utf-8", errors="surrogateescape"))
        changed.append(path)
    for p in skipped:
        print("skipped (vendored):", p)
    if check:
        for p in left:
            print("still on the old licence stack:", p)
        return 1 if left else 0
    print(f"{len(changed)} files rewritten to Apache-2.0, {len(skipped)} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
