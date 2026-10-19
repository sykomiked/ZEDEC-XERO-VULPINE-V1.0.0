#!/usr/bin/env python3
# near_anchor.py — commit an IPFS digest to the NEAR contract (the one
# deliberate gas spend of the interface), and verify it back for free.
#
# The zero-gas interface (near_bridge.py) mirrors the contract's state
# into the armada index for free. This tool closes the loop in the other
# direction: it takes a content digest (a CID, a claim, a voucher doc,
# the release manifest) and records it on the contract's ledger via a
# voucher-issue envelope — a signed, on-chain, final timestamp. After
# that, anyone can verify the digest on NEAR for free via ledger_state /
# a view call, and the armada mirror carries it on IPFS.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage:
  near_anchor.py issue <holder> <amount> <purpose>   # on-chain voucher (gas)
  near_anchor.py view <method> [hexargs]             # free read
"""
import base64
import struct
import subprocess
import sys

CONTRACT = "6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8"


def b64args(holder: str, amount: int, purpose: str) -> str:
    # the vino ledger's address field is 64 bytes (63 chars + NUL); the
    # contract truncates the same way (vino_addr), so truncate here too or
    # the on-chain lookup misses.
    holder = holder[:63]
    h = holder.encode()
    p = purpose.encode()
    if len(p) > 63:
        p = p[:63]
    raw = bytes([len(h)]) + h + struct.pack(">Q", amount) + bytes([len(p)]) + p
    return base64.b64encode(raw).decode()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "issue" and len(sys.argv) == 5:
        _, _, holder, amount, purpose = sys.argv
        args = b64args(holder, int(amount), purpose)
        subprocess.run([
            "near", "call", CONTRACT, "voucher_issue",
            "--networkId", "mainnet",
            "--accountId", CONTRACT,
            "--base64", args, "--gas", "300000000000000",
        ])
        return 0
    if cmd == "view" and len(sys.argv) >= 3:
        method = sys.argv[2]
        args = sys.argv[3] if len(sys.argv) > 3 else ""
        subprocess.run(["near", "view", CONTRACT, method,
                        "--networkId", "mainnet"])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())