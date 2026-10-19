#!/usr/bin/env python3
# archive_pin.py — pin the Filebase archive onto all nine armada nodes.
#
# Pushes real blocks through POST /cisex opcode 0x06 (pin_ingest_block:
# blockstore put + permanent pin + CISEX index + recursive dag-pb link
# enqueue). Sources, in priority order:
#   1. local block_cache/*.dagpb shard blocks
#   2. RELEASE-manifest.json objects <= 2 MiB (fetched from Filebase S3)
#   3. bucket-index.json objects <= 2 MiB
# Stops a node when it answers ACK_FULL; reports per-node pin counts.
# Idempotent per run: node-side dedup makes re-pushed blocks no-ops.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: archive_pin.py [--max-bytes N] [--report OUT.json]"""
import json
import sys
import time
import urllib.request
import concurrent.futures
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from armada_mesh_daemon import NODES, cid_digest  # noqa: E402

ROOT = Path("/Users/36n9/zxv-ipfs-build")
BLOCK_MAX = 2 << 20
S3 = "https://s3.filebase.io/zxv/"


def push_block(ip, data, timeout=15.0):
    req = urllib.request.Request(
        f"http://{ip}/cisex", data=bytes([0x06, 0x70]) + data, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()[:1]
    except urllib.error.HTTPError as e:
        return e.code, b""
    except Exception:
        return 0, b""


def s3_get(key, timeout=60.0):
    req = urllib.request.Request(S3 + urllib.request.quote(key))
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def gather_sources():
    """[(name, bytes-or-(s3key,size))] in push priority order."""
    src = []
    for bp in sorted((ROOT / "fetched/block_cache").glob("*.dagpb")):
        src.append((bp.stem, bp.read_bytes()))
    rm = json.loads((ROOT / "dist/RELEASE-manifest.json").read_text())
    for path, meta in rm.get("objects", {}).items():
        if isinstance(meta, dict) and meta.get("bytes", 0) <= BLOCK_MAX:
            src.append((path, ("zxv-release-20261019/" + path, meta["bytes"])))
    bidx = ROOT / "fetched/bucket-index.json"
    if bidx.exists():
        for e in json.loads(bidx.read_text()).get("entries", []):
            if 0 < e.get("bytes", 0) <= BLOCK_MAX:
                src.append((e["key"], (e["key"], e["bytes"])))
    return src


def main():
    max_bytes = 0
    report = ROOT / "dist/archive_pin_report.json"
    for i, a in enumerate(sys.argv):
        if a == "--max-bytes":
            max_bytes = int(sys.argv[i + 1])
        if a == "--report":
            report = Path(sys.argv[i + 1])
    src = gather_sources()
    print(f"{len(src)} candidate blocks for {len(NODES)} nodes", flush=True)
    done = {ip: set() for _, ip, _ in NODES}
    full = {ip: False for _, ip, _ in NODES}
    pushed_bytes = {ip: 0 for _, ip, _ in NODES}
    t0 = time.time()
    for si, (name, payload) in enumerate(src):
        data = payload if isinstance(payload, bytes) else None
        for name_n, ip, reg in NODES:
            if full[ip] or name in done[ip]:
                continue
            if data is None:
                try:
                    data = s3_get(payload[0])
                except Exception as e:
                    print(f"  fetch fail {name}: {e}", flush=True)
                    break
            st, ack = push_block(ip, data)
            if st == 200 and ack == b"\xa1":
                done[ip].add(name)
                pushed_bytes[ip] += len(data)
            elif ack == b"\xa4" or st == 507:
                full[ip] = True
                print(f"  {name_n} blockstore FULL at {len(done[ip])} blocks "
                      f"({pushed_bytes[ip] >> 20} MiB)", flush=True)
        if si % 25 == 0:
            print(f"[{si}/{len(src)}] " + " ".join(
                f"{n}:{len(done[i])}" for n, i, _ in NODES), flush=True)
        if max_bytes and sum(pushed_bytes.values()) >= max_bytes:
            break
    rep = {"finished": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "elapsed_s": int(time.time() - t0),
           "nodes": {n: {"pinned": len(done[ip]), "bytes": pushed_bytes[ip],
                         "store_full": full[ip]} for n, ip, _ in NODES}}
    report.write_text(json.dumps(rep, indent=2))
    print(json.dumps(rep["nodes"], indent=1), flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
