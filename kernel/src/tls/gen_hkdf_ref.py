#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_hkdf_ref.py: print the reference values test_hkdf_props.c checks.

Every value comes from Python's own hmac and hashlib (RFC 2104 HMAC, RFC 5869
HKDF written out from the RFC text, RFC 8446 section 7.1 key schedule), never
from hkdf.c. The inputs are the byte pattern pat(n, seed): byte i is
(seed + 7*i) mod 256. Run it and paste the output into test_hkdf_props.c
when an input changes:

    python3 -I kernel/src/tls/gen_hkdf_ref.py
"""
import hashlib
import hmac


def pat(n, seed):
    return bytes((seed + 7 * i) % 256 for i in range(n))


def H(b):
    return hashlib.sha256(b).digest()


def mac(k, m):
    return hmac.new(k, m, hashlib.sha256).digest()


def extract(salt, ikm):
    return mac(salt if salt else bytes(32), ikm)


def expand(prk, info, n):
    out, t, c = b"", b"", 1
    while len(out) < n:
        t = mac(prk, t + info + bytes([c]))
        out += t
        c += 1
    return out[:n]


def label(n, lab, ctx):
    full = b"tls13 " + lab
    return n.to_bytes(2, "big") + bytes([len(full)]) + full + bytes([len(ctx)]) + ctx


def expand_label(sec, lab, ctx, n):
    return expand(sec, label(n, lab, ctx), n)


def derive(sec, lab, msgs):
    return expand_label(sec, lab, H(msgs), 32)


def hexs(b):
    return b.hex()


def main():
    rows = []
    # HMAC: key lengths around the 64-byte block, an empty message
    for kl in (0, 1, 63, 64, 65, 130):
        rows.append(("hmac_k%d" % kl, mac(pat(kl, 3), pat(100, 9))))
    rows.append(("hmac_empty", mac(pat(20, 3), b"")))
    # Extract: no salt is HashLen zeros
    rows.append(("extract_nosalt", extract(b"", pat(22, 1))))
    rows.append(("extract_salt", extract(pat(13, 5), pat(22, 1))))
    rows.append(("extract_salt1", extract(pat(1, 5), pat(22, 1))))
    # Expand: lengths across block edges and the 255-block maximum
    prk = extract(pat(13, 5), pat(22, 1))
    for n in (1, 31, 32, 33, 64, 65, 300):
        rows.append(("expand_%d" % n, expand(prk, pat(10, 2), n)))
    rows.append(("expand_max_tail", expand(prk, pat(10, 2), 255 * 32)[-32:]))
    rows.append(("expand_info479_tail", expand(prk, pat(479, 4), 40)))
    # Expand-Label: an output length past 255 (the high length byte)
    rows.append(("label_300", expand_label(prk, b"key", b"", 300)[-16:]))
    rows.append(("label_ctx", expand_label(prk, b"c hs traffic", pat(32, 6), 32)))
    rows.append(("label_64", expand_label(prk, b"a" * 64, pat(32, 6), 32)))
    rows.append(("label_empty", expand_label(prk, b"", b"", 32)))
    # the whole schedule with a PSK, a DHE secret and two transcripts
    psk, dhe = pat(32, 11), pat(32, 12)
    t1, t2 = pat(200, 13), pat(500, 14)
    early = extract(b"", psk)
    hs = extract(derive(early, b"derived", b""), dhe)
    ms = extract(derive(hs, b"derived", b""), bytes(32))
    rows += [("early_psk", early), ("early_zero", extract(b"", bytes(32))),
             ("hs", hs), ("c_hs", derive(hs, b"c hs traffic", t1)),
             ("s_hs", derive(hs, b"s hs traffic", t1)), ("ms", ms),
             ("c_ap", derive(ms, b"c ap traffic", t2)), ("s_ap", derive(ms, b"s ap traffic", t2))]
    s_hs = derive(hs, b"s hs traffic", t1)
    rows.append(("key16", expand_label(s_hs, b"key", b"", 16)))
    rows.append(("iv12", expand_label(s_hs, b"iv", b"", 12)))
    fk = expand_label(s_hs, b"finished", b"", 32)
    rows.append(("finished", mac(fk, H(t1))))
    for name, val in rows:
        print('    {"%s", "%s"},' % (name, hexs(val)))


if __name__ == "__main__":
    main()
