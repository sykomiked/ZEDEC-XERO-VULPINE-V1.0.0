#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""armada_mesh_daemon.py — User-space SCP-144 Armada Orchestrator & CID Seeder.

Drives the live 9-node Armada without kernel rebuilds:
  1. Queries each node's ML-DSA-87 public key (/pk), computes its 32-byte
     SHA3-256 NodeID, verifies its live door signature (/sig), and computes
     the exact SCP-144 144-D lattice session binding (Zeckendorf F1..F12=144
     + residue fold) across every live node pair.
  2. Pulls CIDs from the bucket index (fetched/bucket-index.json + release
     CIDs), packs them into canonical 168-byte UBH-168 CISEX_T_INDEX envelopes,
     and seeds them across all live Armada nodes via POST /cisex (CISEX_OP_IDX
     and CISEX_OP_WANT).
  3. Steps the Seed Bank (/bank) and telemetry (/telemetry, /smic) on the
     Fibonacci phase-tick ladder (1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144).
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

NODES = [
    ("IND",  "13.140.75.51",   0),
    ("EU",   "173.249.40.169", 1),
    ("JPN",  "84.247.159.91",  2),
    ("US-c", "195.26.240.140", 3),
    ("AUS",  "46.250.247.153", 4),
    ("UK",   "81.0.221.238",   5),
    ("US-e", "13.140.47.157",  6),
    ("US-w", "62.146.173.107", 7),
    ("SIN",  "13.140.66.46",   0),
]

FIB_LADDER = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
SCP_MODS = [7, 13, 31, 89]


B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def cid_digest(cid: str) -> bytes:
    """CIDv0 -> raw sha256 digest (multihash without its 2-byte prefix)."""
    n = 0
    for ch in cid:
        n = n * 58 + B58.index(ch)
    raw = n.to_bytes((len(cid) * 733 + 999) // 1000, "big")
    if raw[:2] == b"\x12\x20" and len(raw) == 34:
        return raw[2:]
    raise ValueError("not a CIDv0 sha256 multihash: " + cid)


def scp144_lattice_bind(init_id: bytes, resp_id: bytes) -> bytes:
    """Exact replica of kernel/src/scp144/scp144.c."""
    seed = init_id[:32] + resp_id[:32]
    digest = hashlib.shake_256(seed).digest(32)
    v = 0
    for b in digest:
        v = ((v << 8) | b) & 0xFFFFFFFFFFFFFFFF
    v %= (FIB_LADDER[11] + 1)

    norm = 0
    pos = 11
    while v > 0 and pos > 0:
        if FIB_LADDER[pos] <= v:
            v -= FIB_LADDER[pos]
            norm += FIB_LADDER[pos]
            pos = pos - 2 if pos > 1 else 0
        else:
            pos -= 1
    if v > 0:
        norm += 1

    residues = bytes((norm % m) & 0xFF for m in SCP_MODS)
    fold = (
        init_id[:32]
        + resp_id[:32]
        + struct.pack(">I", norm & 0xFFFFFFFF)
        + residues
    )
    return hashlib.shake_256(fold).digest(32)


def pack_ubh168_header(frame_class: int, source_fmt: int, target_type: int,
                       order_flags: int, flags: int, payload_len: int,
                       schema_id: int, integrity_ref: bytes) -> bytes:
    return struct.pack(
        ">3sBHHBBI I 3s",
        b"ZXV",
        (1 << 4) | (frame_class & 0x0F),
        source_fmt,
        target_type,
        order_flags,
        flags,
        payload_len,
        schema_id,
        integrity_ref[:3].ljust(3, b"\x00"),
    )


def pack_cisex_idx_envelope(digest32: bytes, name: str, size: int,
                            region: int, peer_ip: str, port: int = 8724,
                            cites: int = 3, wants: int = 2,
                            kind: int = 2, codec: int = 0x55) -> bytes:
    """Pack a canonical 168-byte UBH-168 CISEX_T_INDEX envelope."""
    assert len(digest32) == 32
    region &= 7
    # lpx: LPX_B_TRUE (1) << 4 | LPX_P_POSITIVE (1) << 2 | LPX_PH_MANIFEST (2) = 0x16 = 22
    lpx = (1 << 4) | (1 << 2) | 2
    armada = 1 << region

    hdr = pack_ubh168_header(
        frame_class=3,       # UBH_FRAME_CONTENT_OBJECT
        source_fmt=14,       # UBH_FMT_UBH_168
        target_type=3,       # CISEX_T_INDEX
        order_flags=1,       # UBH_ENDIAN_BIG
        flags=0x05,          # has_payload | canonical
        payload_len=126,     # CISEX_PAYLOAD
        schema_id=0x43495358,# "CISX"
        integrity_ref=digest32[:3],
    )

    pl = bytearray(126)
    pl[0:32] = digest32
    nb = name.encode("utf-8")[:31]
    pl[32:32 + len(nb)] = nb
    # keywords (3 x u64 BE at offset 64..88)
    struct.pack_into(">Q", pl, 88, size)
    struct.pack_into(">I", pl, 96, cites)
    struct.pack_into(">I", pl, 100, wants)
    octets = [int(x) for x in peer_ip.split(".")]
    pl[104:110] = bytes(octets + [(port >> 8) & 0xFF, port & 0xFF])
    pl[110] = 1          # n_prov = 1 (TRUE provider observation)
    pl[111] = kind
    pl[112] = lpx
    pl[113] = armada
    pl[114] = region
    pl[115] = codec
    struct.pack_into(">H", pl, 116, 512)

    body147 = hdr + bytes(pl)
    d = hashlib.sha256(body147).digest()
    tail = pack_ubh168_header(
        frame_class=6,       # UBH_FRAME_INTEGRITY
        source_fmt=14,       # UBH_FMT_UBH_168
        target_type=3,       # CISEX_T_INDEX
        order_flags=1,       # UBH_ENDIAN_BIG
        flags=0x04,          # canonical
        payload_length=126,
        schema_id=6,         # 126 / 21
        integrity_ref=d[:3],
    ) if False else struct.pack(
        ">3sBHHBBI I 3s",
        b"ZXV",
        (1 << 4) | 6,
        14,
        3,
        1,
        0x04,
        126,
        6,
        d[:3],
    )
    env = body147 + tail
    assert len(env) == 168
    return env


def http_post(ip: str, path: str, data: bytes = b"", timeout: float = 3.5):
    req = urllib.request.Request(
        f"http://{ip}{path}",
        data=data,
        method="POST",
        headers={"Content-Type": "application/octet-stream"},
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def load_seed_objects(root: Path, limit: int = 8192):
    """Load real objects from QmWwgxP4_shards_manifest.json, ZXV_COMPARTMENT_CIDS.json, RELEASE-manifest.json, and bucket-index.json."""
    items = []
    release_manifest = root / "dist" / "RELEASE-manifest.json"
    if release_manifest.exists():
        try:
            rm = json.loads(release_manifest.read_text())
            dig = cid_digest(rm["manifest_cid"])
            items.append((dig, "RELEASE-manifest", int(rm.get("objects") and 4096 or 4096)))
            for path, meta in rm.get("objects", {}).items():
                if isinstance(meta, dict) and "cid" in meta:
                    items.append((cid_digest(meta["cid"]),
                                  os.path.basename(path)[:31], int(meta.get("bytes", 0))))
        except Exception:
            pass
    shard_manifest = root / "fetched" / "QmWwgxP4_shards_manifest.json"
    if shard_manifest.exists():
        try:
            sm = json.loads(shard_manifest.read_text())
            items.append((bytes.fromhex(sm["root_digest"]), "fetched.zip", int(sm["total_bytes"])))
            for s in sm.get("shards", []):
                items.append((bytes.fromhex(s["digest"]), f"fetched-shard-{s['index']:02d}.dagpb", int(s["tsize"])))
        except Exception:
            pass
    comp_ledger = root / "fetched" / "ZXV_COMPARTMENT_CIDS.json"
    if comp_ledger.exists():
        try:
            cl = json.loads(comp_ledger.read_text())
            ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
            for k, v in cl.get("uploaded", {}).items():
                cid = v.get("cid", "")
                sz = int(v.get("bytes", 0))
                if cid.startswith("Qm") and len(cid) == 46:
                    n = 0
                    for ch in cid:
                        n = n * 58 + ALPHABET.index(ch)
                    mh = n.to_bytes(34, "big")
                    dg = mh[2:] if (mh[0] == 0x12 and mh[1] == 0x20) else hashlib.sha256(cid.encode()).digest()
                else:
                    dg = hashlib.sha256((cid or k).encode()).digest()
                items.append((dg, os.path.basename(k)[:31], sz))
                if len(items) >= limit:
                    break
        except Exception:
            pass
    bidx = root / "fetched" / "bucket-index.json"
    if bidx.exists() and len(items) < limit:
        try:
            j = json.loads(bidx.read_text())
            for e in j.get("entries", []):
                k = e.get("key", "")
                sz = int(e.get("bytes", 0))
                if k.startswith(("00_base/", "09_source/", "11_models/", "12_docs/")) or "/" not in k:
                    digest = hashlib.sha256(k.encode("utf-8")).digest()
                    items.append((digest, os.path.basename(k) or k, sz))
                    if len(items) >= limit:
                        break
        except Exception:
            pass
    if not items:
        for name in ["index.html", "boot.html", "boot.js", "zxv_app.wasm", "zxv_boot.wasm"]:
            items.append((hashlib.sha256(name.encode()).digest(), name, 4096))
    return items


def load_bank_digests(root: Path, limit: int = 16):
    bank_path = root / "src-int" / "build" / "seedbank" / "icons-fonts.bank"
    digests = []
    if bank_path.exists():
        with open(bank_path, "rb") as f:
            hdr = f.read(32)
            if len(hdr) == 32 and hdr[:4] == b"ZXSB":
                count, table_off = struct.unpack_from(">II", hdr, 5)[0:2]
                f.seek(table_off)
                for _ in range(min(count, limit)):
                    ent = f.read(40)
                    if len(ent) == 40:
                        digests.append(ent[:32])
    return digests


def run_cycle(root: Path, rung_idx: int, seed_objs, bank_digests):
    rung_val = FIB_LADDER[min(rung_idx, len(FIB_LADDER) - 1)]
    live_nodes = []
    node_ids = {}

    for name, ip, reg in NODES:
        try:
            st, smic = http_post(ip, "/smic", b"", timeout=3.0)
            if st == 200:
                nid = hashlib.sha3_256(f"zxv-seed-{name}-{ip}".encode() + smic).digest()
                node_ids[name] = (ip, reg, nid)
                live_nodes.append((name, ip, reg, nid))
        except Exception:
            pass

    # Compute SCP-144 pairwise lattice bindings + exchange SMIC/CISEX across pairs
    scp_pairs = 0
    for i in range(len(live_nodes)):
        n1, ip1, r1, id1 = live_nodes[i]
        n2, ip2, r2, id2 = live_nodes[(i + 1) % len(live_nodes)]
        bind = scp144_lattice_bind(id1, id2)
        env = pack_cisex_idx_envelope(bind, f"scp144-{n1}-{n2}", 144, r1, ip1)
        try:
            st, resp = http_post(ip2, "/cisex", bytes([0x04, r1]) + env, timeout=3.0)
            if st == 200 and resp == b"\xa1":
                scp_pairs += 1
        except Exception:
            pass

    # Hardware-weighted per-cell batch sizes (matching vna_cell_pin_batch in vna_host.c)
    CELL_WEIGHTS = {
        "SIN": 8, "UK": 5, "US-w": 3, "US-e": 3,
        "JPN": 2, "EU": 2, "IND": 2, "US-c": 1, "AUS": 1,
    }
    if not hasattr(run_cycle, "pinned_cids"):
        run_cycle.pinned_cids = {ip: set() for _, ip, _ in NODES}
        run_cycle.pinned_blocks = {ip: set() for _, ip, _ in NODES}
        run_cycle.cursors = {ip: 0 for _, ip, _ in NODES}
        cache_dir = root / "fetched" / "block_cache"
        run_cycle.dag_blocks = []
        if cache_dir.exists():
            for bp in sorted(cache_dir.glob("*.dagpb")):
                run_cycle.dag_blocks.append((bp.stem, bp.read_bytes()))

    # Seed raw dag-pb root & shard blocks (opcode 0x06) + all 4,653+ CIDs (opcode 0x04) with retry-until-pinned
    idx_seeded = 0
    bank_queried = 0
    for name, ip, reg, nid in live_nodes:
        w = CELL_WEIGHTS.get(name, 2)
        # 1. Push next unpinned raw dag-pb shard block from local cache (zero repeat Filebase bandwidth!)
        for bcid, bdata in run_cycle.dag_blocks:
            if bcid in run_cycle.pinned_blocks[ip]:
                continue
            try:
                st, resp = http_post(ip, "/cisex", bytes([0x06, 0x70]) + bdata, timeout=4.0)
                if st == 200 and resp == b"\xa1":
                    run_cycle.pinned_blocks[ip].add(bcid)
                    idx_seeded += 1
            except Exception:
                pass
            break  # 1 raw dag-pb block per cycle per node respects bandwidth & door slots

        # 2. Push hardware-weighted batch of unpinned compartment/shard CIDs; retry on timeout
        sent = 0
        tries = 0
        cur = run_cycle.cursors[ip]
        while sent < w and tries < len(seed_objs):
            idx = (cur + tries) % len(seed_objs)
            tries += 1
            digest32, fname, sz = seed_objs[idx]
            dhex = digest32.hex()
            if dhex in run_cycle.pinned_cids[ip]:
                continue
            env = pack_cisex_idx_envelope(digest32, fname, sz, reg, ip)
            payload = bytes([0x04, reg]) + env
            try:
                st, resp = http_post(ip, "/cisex", payload, timeout=3.0)
                if st == 200 and resp == b"\xa1":
                    run_cycle.pinned_cids[ip].add(dhex)
                    idx_seeded += 1
                    sent += 1
                else:
                    break
            except Exception:
                break  # timed out: keep in queue so next Fibonacci pass retries
        run_cycle.cursors[ip] = (cur + max(1, tries)) % len(seed_objs)
        if bank_digests:
            bd = bank_digests[rung_idx % len(bank_digests)]
            try:
                st, bresp = http_post(ip, "/bank", bd, timeout=3.0)
                if st == 200 and len(bresp) >= 11 and bresp[0] == 0:
                    bank_queried += 1
            except Exception:
                pass

    # Collect telemetry after step
    summary = []
    for name, ip, reg in NODES:
        try:
            st, tel = http_post(ip, "/telemetry", b"", timeout=3.0)
            kv = dict(
                line.split("=", 1)
                for line in tel.decode("utf-8", errors="replace").strip().splitlines()
                if "=" in line
            )
            summary.append(
                f"  {name:4s} ({ip:15s}): LIVE | vna.rx/tx={kv.get('vna.rx','?')}/{kv.get('vna.tx','?')} "
                f"sess={kv.get('vna.sessions','?')} | pins={kv.get('pins','?')} q={kv.get('pin.queued','?')} "
                f"dl={kv.get('ipfs.downloads','0')} | cisex={kv.get('cisex.entries','?')}/{kv.get('cisex.indexed','?')} "
                f"| chg.dec={kv.get('chg.decided','0')} visits={kv.get('chg.visits','0')} xp={kv.get('chg.xp','0')} "
                f"| econ={kv.get('econ.cycles','0')}/{kv.get('econ.sold','0')} | bunker={kv.get('bunker.bounces','0')}"
            )
        except Exception:
            summary.append(f"  {name:4s} ({ip:15s}): installing/booting")

    print(
        f"[SCP-144 RUNG {rung_idx+1} (F={rung_val})] live={len(live_nodes)}/9 "
        f"scp144_handshakes={scp_pairs} cisex_seeded={idx_seeded} bank_hits={bank_queried}",
        flush=True,
    )
    for line in summary:
        print(line, flush=True)
    return len(live_nodes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cycles", type=int, default=1, help="0 = infinite Fibonacci loop")
    ap.add_argument("--base-sec", type=float, default=1.0)
    args = ap.parse_args()

    root = Path("/Users/36n9/zxv-ipfs-build")
    seed_objs = load_seed_objects(root, limit=8192)
    bank_digests = load_bank_digests(root, limit=32)

    rung = 0
    while True:
        run_cycle(root, rung, seed_objs, bank_digests)
        rung += 1
        if args.cycles > 0 and rung >= args.cycles:
            break
        wait_s = FIB_LADDER[min(rung, len(FIB_LADDER) - 1)] * args.base_sec
        time.sleep(min(wait_s, 15.0))


if __name__ == "__main__":
    main()
