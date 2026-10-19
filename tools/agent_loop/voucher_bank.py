#!/usr/bin/env python3
# voucher_bank.py — the $YHVH floating voucher system's public ledger.
#
# A voucher is a signed JSON document, pushed to the fleet as a raw block
# (POST /cisex opcode 0x06) and anchored by a CISEX index record (opcode
# 0x04, name "vchr:<asset>:<op>:<id8>"). The index is the public notary:
# anyone may DQ a voucher's digest on any of the nine nodes and prove it
# was anchored fleet-wide. The bank keeps the ledger of record locally —
# chain-folded balances per holder — while the fleet carries the
# attestation.
#
# Document schema (chain-linked, Ed25519-signed):
#   {v:1, op:"issue"|"xfer"|"redeem", asset:"$YHVH", id:"v-<16hex>",
#    issuer:<peer_id|account>, holder:<account>, amount:int,
#    seq:int, prev:<doc sha256 hex|"" for issue>, ts:unix, sig:ed25519}
#
# Double-spend resolution: an issuer's docs form a hash chain; a fork means
# the issuer double-signed and is flagged (canonical tip = longest chain
# from the issuer's genesis).
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage:
  voucher_bank.py init  --keystore F              # mint bank key + genesis
  voucher_bank.py issue --keystore F --holder A --amount N [--purpose S]
  voucher_bank.py xfer  --keystore F --voucher ID --to A
  voucher_bank.py redeem --keystore F --voucher ID
  voucher_bank.py ledger --keystore F             # balances + chain audit
"""
import argparse
import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from armada_mesh_daemon import NODES, cid_digest, pack_cisex_idx_envelope  # noqa: E402
from fleet_reseed import post_http  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: E402
from cryptography.hazmat.primitives import serialization  # noqa: E402

ROOT = Path("/Users/36n9/zxv-ipfs-build")
BANKDIR = ROOT / "dist/voucher_bank"


def load_bank(path):
    if path.exists():
        return json.loads(path.read_text())
    sk = Ed25519PrivateKey.generate()
    seed = sk.private_bytes(serialization.Encoding.Raw,
                            serialization.PrivateFormat.Raw,
                            serialization.NoEncryption())
    bank = {"seed_hex": seed.hex(),
            "pub_hex": sk.public_key().public_bytes(
                serialization.Encoding.Raw,
                serialization.PublicFormat.Raw).hex(),
            "docs": {},        # doc_digest -> doc
            "vouchers": {},    # voucher id -> {"chain":[digests], "holder":..., "amount":..., "redeemed":bool}
            "seq": 0}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(bank, indent=2))
    path.chmod(0o600)
    return bank


def save_bank(bank, path):
    path.write_text(json.dumps(bank, indent=2))


def sign_doc(bank, doc):
    sk = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(bank["seed_hex"]))
    payload = json.dumps({k: v for k, v in doc.items() if k != "sig"},
                         sort_keys=True, separators=(",", ":")).encode()
    doc["sig"] = sk.sign(payload).hex()
    return doc


def doc_digest(doc):
    payload = json.dumps(doc, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(payload).digest()


def push_block(ip, raw, timeout=10.0):
    req = urllib.request.Request(
        f"http://{ip}/cisex", data=bytes([0x06, 0x55]) + raw, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status == 200 and r.read()[:1] == b"\xa1"
    except Exception:
        return False


def anchor(ip, digest32, name, region, timeout=8.0):
    env = pack_cisex_idx_envelope(digest32, name, 0, region, ip)
    req = urllib.request.Request(
        f"http://{ip}/cisex", data=bytes([0x04, region]) + env, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status == 200 and r.read()[:1] == b"\xa1"
    except Exception:
        return False


def publish(bank, doc):
    """Push the doc block + anchor its index record on every node."""
    dg = doc_digest(doc)
    raw = json.dumps(doc, sort_keys=True, separators=(",", ":")).encode()
    name = f"vchr:$YHVH:{doc['op'][:1]}:{dg[:4].hex()}"
    ok_blocks = ok_idx = 0
    for n, ip, reg in NODES:
        if push_block(ip, raw):
            ok_blocks += 1
        if anchor(ip, dg, name, reg):
            ok_idx += 1
    bank["docs"][dg.hex()] = doc
    return dg, ok_blocks, ok_idx


def chain_of(bank, vid):
    v = bank["vouchers"].get(vid)
    return v["chain"] if v else []


def balances(bank):
    out = {}
    for vid, v in bank["vouchers"].items():
        if not v["redeemed"]:
            out[v["holder"]] = out.get(v["holder"], 0) + v["amount"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["init", "issue", "xfer", "redeem", "ledger"])
    ap.add_argument("--keystore", type=Path, default=BANKDIR / "bank.json")
    ap.add_argument("--holder", default="BSC-0-1870892d")
    ap.add_argument("--to", default="")
    ap.add_argument("--amount", type=int, default=0)
    ap.add_argument("--voucher", default="")
    ap.add_argument("--purpose", default="$YHVH float")
    a = ap.parse_args()

    bank = load_bank(a.keystore)
    if a.cmd == "init":
        print(f"bank key {bank['pub_hex'][:32]}… ledger {a.keystore} "
              f"({len(bank['vouchers'])} vouchers)")
        return 0

    if a.cmd == "ledger":
        bal = balances(bank)
        print(f"{len(bank['vouchers'])} vouchers, {len(bank['docs'])} docs, "
              f"seq {bank['seq']}")
        for h, b in sorted(bal.items()):
            print(f"  {h}: {b}")
        bad = [vid for vid, v in bank["vouchers"].items()
               if len(set(v["chain"])) != len(v["chain"])]
        if bad:
            print("CHAIN ANOMALY:", bad)
        return 0

    if a.cmd == "issue":
        if a.amount <= 0:
            print("issue needs --amount > 0", file=sys.stderr)
            return 2
        bank["seq"] += 1
        vid = f"v-{bank['seq']:08x}-{int(time.time()):x}"
        doc = sign_doc(bank, {"v": 1, "op": "issue", "asset": "$YHVH", "id": vid,
                              "issuer": bank["pub_hex"], "holder": a.holder,
                              "amount": a.amount, "seq": bank["seq"],
                              "prev": "", "ts": int(time.time()),
                              "purpose": a.purpose})
        dg, nb, ni = publish(bank, doc)
        bank["vouchers"][vid] = {"chain": [dg.hex()], "holder": a.holder,
                                 "amount": a.amount, "redeemed": False}
        save_bank(bank, a.keystore)
        print(f"issued {vid} {a.amount} -> {a.holder} "
              f"(blocks {nb}/9, anchors {ni}/9, digest {dg.hex()[:16]})")
        return 0

    for op in ("xfer", "redeem"):
        if a.cmd == op:
            v = bank["vouchers"].get(a.voucher)
            if not v:
                print(f"no voucher {a.voucher}", file=sys.stderr)
                return 2
            if v["redeemed"]:
                print(f"{a.voucher} already redeemed", file=sys.stderr)
                return 2
            if op == "xfer" and not a.to:
                print("xfer needs --to <account>", file=sys.stderr)
                return 2
            bank["seq"] += 1
            prev = v["chain"][-1]
            holder = a.to if op == "xfer" else v["holder"]
            doc = sign_doc(bank, {"v": 1, "op": op, "asset": "$YHVH",
                                  "id": a.voucher, "issuer": bank["pub_hex"],
                                  "holder": holder, "amount": v["amount"],
                                  "seq": bank["seq"], "prev": prev,
                                  "ts": int(time.time())})
            dg, nb, ni = publish(bank, doc)
            v["chain"].append(dg.hex())
            v["holder"] = holder
            if op == "redeem":
                v["redeemed"] = True
            save_bank(bank, a.keystore)
            print(f"{op} {a.voucher} -> {holder} "
                  f"(blocks {nb}/9, anchors {ni}/9)")
            return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
