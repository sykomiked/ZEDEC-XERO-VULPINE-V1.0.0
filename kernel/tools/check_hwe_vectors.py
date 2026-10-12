#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Check the standard-engine golden vectors (src/hwemu/hwe_vectors.h)
against independent implementations: hashlib for SHA-256, SHA3 and SHAKE,
hmac for HMAC-SHA256, math.gcd for gcd. Logic64 and tensor are defined by
the open C reference itself (lpres.c, zt_kern.h) and are not re-derived."""
import hashlib
import hmac
import math
import re
import sys
from pathlib import Path

H = Path(__file__).resolve().parent.parent / "src/hwemu/hwe_vectors.h"
FN = {0x5A450001: "sha256", 0x5A450002: "keccak", 0x5A450003: "mac", 0x5A450004: "gcd"}


def arr(text, name):
    m = re.search(r"static const uint8_t %s\[\] = \{([^}]*)\};" % name, text)
    return bytes(int(x, 16) for x in m.group(1).split(",") if x.strip())


def main():
    t = H.read_text()
    checked = 0
    for m in re.finditer(r"#define HWE_V(\d+) \{0x([0-9a-f]+)u, (\d+)u, hwe_in\d+, (\d+)u", t):
        i, fn, op, n = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), int(m.group(4))
        if fn not in FN:
            continue
        x, y = arr(t, "hwe_in%d" % i)[:n], arr(t, "hwe_out%d" % i)
        kind = FN[fn]
        if kind == "sha256":
            want = hashlib.sha256(x).digest()
        elif kind == "keccak":
            if op == 0:
                want = hashlib.sha3_256(x).digest()
            elif op == 1:
                want = hashlib.sha3_512(x).digest()
            else:
                k = int.from_bytes(x[:2], "little")
                h = hashlib.shake_128 if op == 2 else hashlib.shake_256
                want = h(x[2:]).digest(k)
        elif kind == "mac":
            k = int.from_bytes(x[:2], "little")
            want = hmac.new(x[2 : 2 + k], x[2 + k :], hashlib.sha256).digest()
        else:
            want = b"".join(
                math.gcd(int.from_bytes(x[j : j + 8], "little"), int.from_bytes(x[j + 8 : j + 16], "little")).to_bytes(8, "little")
                for j in range(0, len(x), 16)
            )
        if want != y:
            print("MISMATCH vector %d (%s op %d)" % (i, kind, op))
            return 1
        checked += 1
    print("hwe vectors: %d checked against independent implementations" % checked)
    return 0 if checked >= 10 else 1


if __name__ == "__main__":
    sys.exit(main())
