#!/usr/bin/env python3
# near_bridge.py — the zero-gas NEAR <-> ZXV armada interface.
#
# View calls on NEAR are free: the RPC node executes them read-only, no
# transaction, no gas, no fee. This daemon exploits exactly that:
#
#   poll (free RPC)  ->  sign a state document  ->  push to Filebase + all
#   nine armada nodes (opcode 0x06)  ->  anchor in the CISEX index (0x04)
#
# Nothing here ever calls a state-changing NEAR method, so the armada and
# the IPFS side pay nothing. The contract's state becomes a periodically
# re-observed, content-addressed, fleet-indexed document: anyone can DQ
# `near:state:<ts>` on any node, or read it via /ipfs/<CID>, or re-run the
# free RPC themselves. Writes to NEAR (gas) are reserved for periodic
# anchors, issued deliberately, not per interaction.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage:
  near_bridge.py once            # single poll + publish cycle
  near_bridge.py loop            # poll on a Fibonacci ladder (daemon)
"""
import base64
import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from armada_mesh_daemon import NODES, pack_cisex_idx_envelope  # noqa: E402
from fleet_reseed import post_http  # noqa: E402

ROOT = Path("/Users/36n9/zxv-ipfs-build")
CONTRACT = "6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8"
RPC = "https://rpc.mainnet.near.org"
FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377]


def near_view(method: str, args: bytes = b"", timeout: float = 15.0):
    """Free read-only call against the NEAR RPC: no gas, no tx, no fee."""
    body = {"jsonrpc": "2.0", "id": "zxv", "method": "query", "params": {
        "request_type": "call_function", "finality": "final",
        "account_id": CONTRACT, "method_name": method,
        "args_base64": base64.b64encode(args).decode()}}
    req = urllib.request.Request(RPC, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        d = json.loads(r.read())
    res = d.get("result", {})
    if res.get("error"):
        raise RuntimeError(f"{method}: {res['error']}")
    return bytes(res.get("result", []))


def poll_state():
    """Observe the contract's public state with free view calls."""
    out = {}
    out["ts"] = int(time.time())
    out["contract"] = CONTRACT
    out["network"] = "mainnet"
    for m in ("ledger_state", "chain_verify", "read_probe"):
        try:
            out[m] = near_view(m).decode("latin1")
        except Exception as e:
            out[m] = f"ERR {e}"
    try:
        bind = near_view("scp144_bind",
                         bytes.fromhex(
                             "6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8")
                         + b"ZXV-ROOT-RESP-0001" + b"\x00" * 17)
        out["scp144_bind"] = bind.hex()
    except Exception as e:
        out["scp144_bind"] = f"ERR {e}"
    return out


def publish(state: dict):
    """Sign-free (the state is public facts), content-addressed, fleet-wide."""
    doc = json.dumps(state, sort_keys=True, separators=(",", ":")).encode()
    dg = hashlib.sha256(doc).digest()
    name = f"near:state:{state['ts']}"
    ok_b = ok_i = 0
    for n, ip, reg in NODES:
        try:
            r = urllib.request.Request(f"http://{ip}/cisex",
                                       data=bytes([0x06, 0x55]) + doc, method="POST")
            if urllib.request.urlopen(r, timeout=10).read()[:1] == b"\xa1":
                ok_b += 1
        except Exception:
            pass
        try:
            env = pack_cisex_idx_envelope(dg, name, len(doc), reg, ip)
            r = urllib.request.Request(f"http://{ip}/cisex",
                                       data=bytes([0x04, reg]) + env, method="POST")
            if urllib.request.urlopen(r, timeout=10).read()[:1] == b"\xa1":
                ok_i += 1
        except Exception:
            pass
    state["digest"] = dg.hex()
    state["name"] = name
    state["blocks"] = ok_b
    state["anchors"] = ok_i
    return state


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "once"
    if mode not in ("once", "loop"):
        print(__doc__)
        return 2
    rung = 0
    while True:
        t0 = time.time()
        try:
            st = poll_state()
            st = publish(st)
            print(f"[near_bridge] {st['ts']} digest {st['digest'][:16]} "
                  f"blocks {st['blocks']}/9 anchors {st['anchors']}/9")
            print(f"  ledger : {st.get('ledger_state','?')[:80]}")
            print(f"  chain  : {st.get('chain_verify','?')}")
        except Exception as e:
            print(f"[near_bridge] cycle failed: {e}")
        if mode == "once":
            return 0
        wait = FIB[rung % len(FIB)]
        rung += 1
        time.sleep(max(1.0, wait - (time.time() - t0)))


if __name__ == "__main__":
    raise SystemExit(main())