#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_raffle_ref.py — an independent reference model of raffle.h.

Re-implements the fee, minting, the Fibonacci trigger, the hash chain, the
epoch header, commit-reveal, the unbiased draw and the split with Python big
integers and hashlib, runs the scripted scenario that test_raffle.c also
runs, and writes "key value" lines that the C test must reproduce exactly.

Usage: gen_raffle_ref.py OUT
"""
import hashlib
import sys

FEE = (987, 61000)
SKIM = (987, 61000)
FIB_INDEX = 22  # F(22) = 17,711 fee units
MAX_MULT = 13
SHARE = (8, 21)
EPOCHS = 3
PARTIES = 3


def u64(v):
    return v.to_bytes(8, "little")


def sha3(b):
    return hashlib.sha3_256(b).digest()


def fib(n):
    a, b = 1, 1
    for _ in range(2, n):
        a, b = b, a + b
    return a if n == 1 else b


def ident(tag, i):
    return sha3(tag + bytes([i]))


def commitment(epoch, party, secret):
    return sha3(b"ZXV-RAFFLE-COMMIT" + u64(epoch) + party + secret)


def amount_of(j):
    return j % 61 if j % 5 == 0 else 62 + (j * 104_729) % 300_000


def run_epoch(epoch, prev_link, out):
    chain = sha3(b"ZXV-RAFFLE-GENESIS" + u64(epoch) + prev_link)
    threshold = fib(FIB_INDEX)
    parties = [ident(b"party", i) for i in range(PARTIES)]
    secrets = [sha3(b"secret" + u64(epoch) + bytes([i])) for i in range(PARTIES)]
    commits = [commitment(epoch, p, s) for p, s in zip(parties, secrets)]
    tickets = pool = 0
    ranges = []
    k = 0
    refused = 0
    while tickets < threshold:
        owner = ident(b"owner", k % 7)
        j = k + 1000 * epoch
        amount = amount_of(j)
        mult = 1 + j % 3
        k += 1
        fee = amount * FEE[0] * mult // FEE[1]
        n = fee  # one ticket per fee unit
        held = sum(c for f, c, o in ranges if o == owner)
        cap = max(threshold * SHARE[0] // SHARE[1], 1)
        if n == 0 or held + n > cap:
            refused += 1
            continue
        first = tickets
        ranges.append((first, n, owner))
        chain = sha3(b"ZXV-RAFFLE-ENTRY" + chain + owner + u64(amount) + u64(mult) + u64(fee)
                     + u64(first) + u64(n) + b"\x00")
        pool += fee
        tickets += n
    hdr = (b"ZXV-RAFFLE-HDR3" + u64(epoch) + u64(FEE[0]) + u64(FEE[1]) + u64(SKIM[0])
           + u64(SKIM[1]) + u64(threshold) + u64(MAX_MULT) + u64(SHARE[0]) + u64(SHARE[1]) + u64(tickets) + u64(pool) + chain
           + u64(PARTIES))
    for p, c in zip(parties, commits):
        hdr += p + c
    hh = sha3(hdr)
    seed_in = b"ZXV-RAFFLE-SEED" + hh
    for s in secrets:
        seed_in += b"\x01" + s
    seed = sha3(seed_in)
    r = (1 << 64) % tickets
    win = None
    for ctr in range(128):
        x = int.from_bytes(hashlib.shake_256(b"ZXV-RAFFLE-DRAW" + seed + u64(ctr)).digest(8),
                           "little")
        if x < (1 << 64) - r:
            win = x % tickets
            break
    assert win is not None
    winner = next(o for f, n, o in ranges if f <= win < f + n)
    skim = pool * SKIM[0] // SKIM[1]
    pot = pool - skim
    assert skim + pot == pool
    link = sha3(b"ZXV-RAFFLE-SETTLE" + u64(epoch) + hh + seed + u64(win) + winner + u64(pool)
                + u64(skim) + u64(pot))
    for key, val in (("tickets", tickets), ("pool", pool), ("entries", len(ranges)),
                     ("refused", refused), ("winning", win), ("skim", skim), ("pot", pot)):
        out.append(f"e{epoch}.{key} {val}")
    for key, val in (("header", hh), ("seed", seed), ("winner", winner), ("link", link)):
        out.append(f"e{epoch}.{key} {val.hex()}")
    return link


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    assert fib(18) == 2584 and fib(20) == 6765 and fib(FIB_INDEX) == 17711
    out = []
    link = bytes(32)
    for e in range(1, EPOCHS + 1):
        link = run_epoch(e, link, out)
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
