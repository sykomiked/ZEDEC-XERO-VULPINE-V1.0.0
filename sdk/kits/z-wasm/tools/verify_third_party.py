# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Check that third_party/wasm3 is byte-identical to what THIRD_PARTY.md
records: every listed file matches its SHA-256, and no unlisted file was
added under third_party/wasm3.

usage: python3 tools/verify_third_party.py   (run from sdk/kits/z-wasm)
"""
import hashlib
import os
import re
import sys

ROOT = os.path.join("third_party", "wasm3")


def main():
    with open("THIRD_PARTY.md", encoding="utf-8") as f:
        text = f.read()
    block = re.search(r"```sha256\n(.*?)```", text, re.S)
    if not block:
        print("verify-third-party: no ```sha256 block in THIRD_PARTY.md")
        return 1
    want = {}
    for line in block.group(1).splitlines():
        if line.strip():
            digest, path = line.split()
            want[path] = digest
    bad = 0
    for path, digest in sorted(want.items()):
        full = os.path.join(ROOT, path)
        try:
            with open(full, "rb") as f:
                got = hashlib.sha256(f.read()).hexdigest()
        except OSError:
            print(f"missing: {full}")
            bad += 1
            continue
        if got != digest:
            print(f"changed: {full}")
            bad += 1
    for dirpath, _, names in os.walk(ROOT):
        for n in names:
            rel = os.path.relpath(os.path.join(dirpath, n), ROOT).replace(os.sep, "/")
            if rel not in want:
                print(f"not recorded in THIRD_PARTY.md: {os.path.join(ROOT, rel)}")
                bad += 1
    if bad:
        print("verify-third-party: FAILED")
        return 1
    print(f"verify-third-party: OK ({len(want)} wasm3 files match THIRD_PARTY.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
