#!/usr/bin/env python3
# legacy_bridge.py — the armada's legacy-network compatibility layer.
#
# Publishes each of the nine VNA nodes to the public IPFS routing tables as
# an IPNI (InterPlanetary Network Indexer) provider over the HTTP trustless
# gateway transport — the same /ipfs/<CID> door every node already serves.
# No kernel changes: the bridge builds signed IPNI advertisement chains
# (dag-cbor EntryChunk -> Advertisement, libp2p record.Seal signature) for
# a per-node Ed25519 libp2p persona, serves the immutable chain files on
# /ipni/v1/ad/{CID} from any static HTTP host, and announces the head to
# public indexers (cid.contact ingest, indexing.ipni.network).
#
# Staged activation: run --serve for the publisher surface any time, but
# --gate watches fleet /telemetry cisex.entries and only announces once the
# index crosses --threshold entries on every live node and dwells there for
# --dwell seconds — the "index a huge amount first, then go online all at
# once" launch profile.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage:
  legacy_bridge.py keygen [--keystore FILE]          # mint 9 libp2p personas
  legacy_bridge.py build [--keystore F] [--out DIR]  # emit ipni/v1/ad tree
  legacy_bridge.py serve [--dir DIR] [--port 8081]   # publisher HTTP surface
  legacy_bridge.py announce [--dir DIR] [--indexer URL ...]
  legacy_bridge.py gate --threshold N --dwell S      # wait, then build+announce
"""
import argparse
import base64
import hashlib
import http.server
import json
import sys
import threading
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from armada_mesh_daemon import NODES, cid_digest, load_seed_objects  # noqa: E402
from fleet_reseed import post_http  # noqa: E402

from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey, Ed25519PublicKey)
from cryptography.hazmat.primitives import serialization

ROOT = Path("/Users/36n9/zxv-ipfs-build")
KEYSTORE = ROOT / "dist/legacy_bridge/keystore.json"
OUTDIR = ROOT / "dist/legacy_bridge/site"
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
B32 = "abcdefghijklmnopqrstuvwxyz234567"

AD_DOMAIN = b"indexer"
AD_CODEC = b"/indexer/ingest/adSignature"
META_HTTP = b"\xa0\x12"            # uvarint 0x0920 transport-ipfs-gateway-http
CONTEXT_ID = b"zxv-vna-armada-v1"
CID_UNDEF = b"\x12\x20" + hashlib.sha256(b"").digest()  # cid.Undef.Bytes()
CHUNK_MHS = 60000                  # ~2 MB per EntryChunk


# ---------- multiformats / protobuf primitives --------------------------------

def uvarint(n):
    out = bytearray()
    while n >= 0x80:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    out.append(n)
    return bytes(out)


def b58enc(b):
    n = int.from_bytes(b, "big")
    s = ""
    while n:
        n, r = divmod(n, 58)
        s = B58[r] + s
    pad = 0
    for x in b:
        if x == 0:
            pad += 1
        else:
            break
    return "1" * pad + (s or "")


def pb_field(num, wire, val):
    key = uvarint((num << 3) | wire)
    if wire == 0:
        return key + uvarint(val)
    return key + uvarint(len(val)) + val


def pubkey_proto(pk32):
    return pb_field(1, 0, 1) + pb_field(2, 2, pk32)          # KeyType ED25519=1


def peer_id(pk32):
    """base58 peer id — identity multihash of the protobuf public key."""
    proto = pubkey_proto(pk32)
    mh = b"\x00" + uvarint(len(proto)) + proto
    return b58enc(mh)


def cidv1_dagcbor(digest32):
    return b"\x01\x71\x12\x20" + digest32


def cid_b32(cid_bytes):
    """base32-lower of a CIDv1."""
    bits = 0
    acc = 0
    out = []
    for byte in cid_bytes:
        acc = (acc << 8) | byte
        bits += 8
        while bits >= 5:
            bits -= 5
            out.append(B32[(acc >> bits) & 0x1F])
    if bits:
        out.append(B32[(acc << (5 - bits)) & 0x1F])
    return "b" + "".join(out)


# ---------- deterministic dag-cbor encoder ------------------------------------

def cbor_len(major, n):
    if n < 24:
        return bytes([(major << 5) | n])
    if n < 256:
        return bytes([(major << 5) | 24, n])
    if n < 65536:
        return bytes([(major << 5) | 25]) + n.to_bytes(2, "big")
    return bytes([(major << 5) | 26]) + n.to_bytes(4, "big")


def cbor(b):
    if isinstance(b, bool):
        return b"\xf5" if b else b"\xf4"
    if isinstance(b, bytes):
        return cbor_len(2, len(b)) + b
    if isinstance(b, str):
        e = b.encode()
        return cbor_len(3, len(e)) + e
    if isinstance(b, list):
        return cbor_len(4, len(b)) + b"".join(cbor(x) for x in b)
    if isinstance(b, Link):
        return b"\xd8\x2a" + cbor(b"\x00" + b.cid)
    if isinstance(b, dict):
        keys = sorted(b, key=lambda k: (len(k.encode()), k.encode()))
        return cbor_len(5, len(b)) + b"".join(cbor(k) + cbor(b[k]) for k in keys)
    raise TypeError(type(b))


class Link:
    def __init__(self, cid_bytes):
        self.cid = cid_bytes


def ipld_node(obj):
    data = cbor(obj)
    return data, cidv1_dagcbor(hashlib.sha256(data).digest())


# ---------- keystore ------------------------------------------------------------

def gen_identities(path):
    ids = {}
    for name, ip, reg in NODES:
        sk = Ed25519PrivateKey.generate()
        seed = sk.private_bytes(serialization.Encoding.Raw,
                                serialization.PrivateFormat.Raw,
                                serialization.NoEncryption())
        pk = sk.public_key().public_bytes(serialization.Encoding.Raw,
                                          serialization.PublicFormat.Raw)
        ids[name] = {"ip": ip, "region": reg,
                     "seed_hex": seed.hex(), "pub_hex": pk.hex(),
                     "peer_id": peer_id(pk)}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(ids, indent=2))
    path.chmod(0o600)
    return ids


def load_identities(path):
    if path.exists():
        return json.loads(path.read_text())
    return gen_identities(path)


def sign(priv_seed_hex, msg):
    sk = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(priv_seed_hex))
    return sk.sign(msg)


def seal(priv_seed_hex, pub_hex, domain, codec, payload):
    """libp2p record.Seal: protobuf Envelope over uvarint-prefixed fields."""
    unsigned = (uvarint(len(domain)) + domain +
                uvarint(len(codec)) + codec +
                uvarint(len(payload)) + payload)
    sig = sign(priv_seed_hex, unsigned)
    env = (pb_field(1, 2, pubkey_proto(bytes.fromhex(pub_hex))) +
           pb_field(2, 2, codec) +
           pb_field(3, 2, payload) +
           pb_field(5, 2, sig))
    return env


# ---------- advertisement chain -----------------------------------------------

def build_chain(ids, multihashes, outdir):
    """Emit one signed ad chain per node into outdir/ipni/v1/ad/<cid> files."""
    ad_dir = outdir / "ipni" / "v1" / "ad"
    ad_dir.mkdir(parents=True, exist_ok=True)
    heads = {}
    for name, ident in ids.items():
        chunks = [multihashes[i:i + CHUNK_MHS]
                  for i in range(0, len(multihashes), CHUNK_MHS)] or [[]]
        next_link = None
        for chunk in reversed(chunks):
            node = {"Entries": [bytes(mh) for mh in chunk]}
            if next_link is not None:
                node["Next"] = next_link
            data, cid = ipld_node(node)
            (ad_dir / cid_b32(cid)).write_bytes(data)
            next_link = Link(cid)
        entries_link = next_link.cid if next_link else CID_UNDEF

        sig_buf = (CID_UNDEF + entries_link +
                   ident["peer_id"].encode() +
                   f'/ip4/{ident["ip"]}/tcp/80/http'.encode() +
                   META_HTTP + b"\x00")
        adv_id = b"\x12\x20" + hashlib.sha256(sig_buf).digest()
        env = seal(ident["seed_hex"], ident["pub_hex"],
                   AD_DOMAIN, AD_CODEC, adv_id)
        ad = {"Addresses": [f'/ip4/{ident["ip"]}/tcp/80/http'],
              "ContextID": CONTEXT_ID, "Entries": Link(entries_link),
              "IsRm": False, "Metadata": META_HTTP,
              "Provider": ident["peer_id"], "Signature": env}
        data, acid = ipld_node(ad)
        (ad_dir / cid_b32(acid)).write_bytes(data)
        heads[name] = {"ad_cid": cid_b32(acid), "peer_id": ident["peer_id"],
                       "addr": f'/ip4/{ident["ip"]}/tcp/80/http'}
    return heads


def write_head(outdir, head_cid_b32, ident, topic=b"/indexer/ingest/mainnet"):
    """Serialized SignedHead: sig over (head CID bytes + topic UTF-8)."""
    head_bin = cid_b32_to_bin(head_cid_b32)
    sig = sign(ident["seed_hex"], head_bin + topic)
    node = {"head": Link(head_bin), "topic": topic.decode(),
            "pubkey": pubkey_proto(bytes.fromhex(ident["pub_hex"])), "sig": sig}
    data, _ = ipld_node(node)
    (outdir / "ipni" / "v1" / "ad" / "head").write_bytes(data)


def cid_b32_to_bin(cidstr):
    """b32 'bafy...' string -> CID bytes."""
    acc = 0
    bits = 0
    out = bytearray()
    for ch in cidstr[1:]:
        acc = (acc << 5) | B32.index(ch)
        bits += 5
        while bits >= 8:
            bits -= 8
            out.append((acc >> bits) & 0xFF)
    return bytes(out)


# ---------- publisher + announce -----------------------------------------------

class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def serve(outdir, port):
    import functools
    h = functools.partial(Handler, directory=str(outdir))
    http.server.ThreadingHTTPServer(("", port), h).serve_forever()


def announce(heads, publisher_addr, indexers):
    for idx in indexers:
        for name, h in heads.items():
            body = json.dumps({"Cid": {"/": h["ad_cid"]},
                               "Addrs": [publisher_addr]}).encode()
            req = urllib.request.Request(idx.rstrip("/") + "/announce",
                                         data=body, method="PUT",
                                         headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=20) as r:
                    print(f"announce {name} {h['ad_cid'][:20]} -> {idx}: {r.status}",
                          flush=True)
            except urllib.error.HTTPError as e:
                print(f"announce {name} -> {idx}: HTTP {e.code} {e.read()[:200]}",
                      flush=True)
            except Exception as e:
                print(f"announce {name} -> {idx}: {e}", flush=True)


# ---------- staged activation gate ----------------------------------------------

def fleet_min_entries():
    vals = []
    for name, ip, reg in NODES:
        try:
            _, body = post_http(ip, "/telemetry", b"", timeout=8)
            for line in body.decode().splitlines():
                if line.startswith("cisex.entries="):
                    vals.append((name, int(line.split("=")[1])))
        except Exception:
            vals.append((name, -1))
    return vals


def gate(threshold, dwell, outdir, ks_path, publisher_addr, indexers):
    print(f"gate: waiting for min(cisex.entries) >= {threshold} held {dwell}s",
          flush=True)
    stable_since = None
    while True:
        vals = fleet_min_entries()
        mn = min(v for _, v in vals)
        status = " ".join(f"{n}={v}" for n, v in vals)
        if mn >= threshold:
            if stable_since is None:
                stable_since = time.time()
                print(f"gate: threshold met ({status}) — dwell started", flush=True)
            elif time.time() - stable_since >= dwell:
                break
        else:
            stable_since = None
            print(f"gate: {status} (min {mn} < {threshold})", flush=True)
        time.sleep(60)
    print("gate: threshold held — building and announcing", flush=True)
    ids = load_identities(ks_path)
    mhs = collect_multihashes()
    heads = build_chain(ids, mhs, outdir)
    (outdir / "heads.json").write_text(json.dumps(heads, indent=2))
    announce(heads, publisher_addr, indexers)
    print("gate: activation complete", flush=True)


def collect_multihashes():
    """Every multihash the fleet advertises: full seed object catalogue."""
    mhs = []
    for dg, name, sz in load_seed_objects(ROOT):
        mhs.append(b"\x12\x20" + dg)
    seen = set()
    out = []
    for m in mhs:
        if m not in seen:
            seen.add(m)
            out.append(m)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["keygen", "build", "serve", "announce", "gate"])
    ap.add_argument("--keystore", type=Path, default=KEYSTORE)
    ap.add_argument("--dir", type=Path, default=OUTDIR)
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--threshold", type=int, default=50000)
    ap.add_argument("--dwell", type=float, default=3600.0)
    ap.add_argument("--publisher-addr", default="/dns4/ipni.vinea.zedec.ai/tcp/443/https")
    ap.add_argument("--indexer", action="append",
                    default=["https://ingest.cid.contact", "https://indexing.ipni.network"])
    a = ap.parse_args()

    if a.cmd == "keygen":
        ids = gen_identities(a.keystore)
        for n, i in ids.items():
            print(f"{n}: {i['peer_id']}")
    elif a.cmd == "build":
        ids = load_identities(a.keystore)
        mhs = collect_multihashes()
        print(f"building chains: {len(mhs)} multihashes x {len(ids)} providers")
        heads = build_chain(ids, mhs, a.dir)
        (a.dir / "heads.json").write_text(json.dumps(heads, indent=2))
        for n, h in heads.items():
            print(f"{n}: ad {h['ad_cid']}")
        print(f"site tree at {a.dir}/ipni/v1/ad/")
    elif a.cmd == "serve":
        print(f"serving {a.dir} on :{a.port}")
        serve(a.dir, a.port)
    elif a.cmd == "announce":
        heads = json.loads((a.dir / "heads.json").read_text())
        announce(heads, a.publisher_addr, a.indexer)
    elif a.cmd == "gate":
        gate(a.threshold, a.dwell, a.dir, a.keystore, a.publisher_addr, a.indexer)


if __name__ == "__main__":
    raise SystemExit(main())
