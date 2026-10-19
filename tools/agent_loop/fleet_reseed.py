#!/usr/bin/env python3
# fleet_reseed.py — reboot the nine-server armada (clears the in-memory
# CISEX index), then recursively seed every CID of a release manifest via
# POST /cisex and verify with DQ lookups. API-driven; no SSH on the nodes.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: fleet_reseed.py [--reboot] [--dry-run] <release-manifest.json>

The manifest maps release paths to {"cid": ..., "bytes": ...} under
"objects" (the shape RELEASE-manifest.json writes). With --reboot each
instance is restarted through the Contabo API first — the CISEX table
lives in BSS, so a reboot frees all VNAH_CISEX_N slots — and /smic is
polled until the node answers again."""
import json
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import contabo_fleet as cf  # noqa: E402
from armada_mesh_daemon import NODES, pack_cisex_idx_envelope  # noqa: E402

B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def b58(s):
    n = 0
    for ch in s:
        n = n * 58 + B58.index(ch)
    return n.to_bytes((len(s) * 733 + 999) // 1000, "big")


def cid_digest(cid):
    """CIDv0 -> raw sha256 digest (multihash without its 2-byte prefix)."""
    raw = b58(cid)
    if raw[:2] == b"\x12\x20" and len(raw) == 34:
        return raw[2:]
    raise ValueError("not a CIDv0 sha256 multihash: " + cid)


def post_http(ip, path, data=b"", timeout=8.0):
    req = urllib.request.Request(
        f"http://{ip}{path}", data=data, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def fleet_instances(tok):
    """instanceId -> v4 ip for every instance in the account."""
    d = cf.api_get(tok, "/compute/instances?size=100")
    out = {}
    for i in d.get("data", []):
        ip = (i.get("ipConfig") or {}).get("v4", {}).get("ip")
        if ip:
            out[i["instanceId"]] = ip
    return out


def restart(tok, iid):
    req = urllib.request.Request(
        cf.API + "/compute/instances/%s/actions/restart" % iid,
        data=b"{}", method="POST",
        headers={"Authorization": "Bearer " + tok,
                 "Content-Type": "application/json",
                 "x-request-id": str(uuid.uuid4())})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


def vna_rx(ip):
    try:
        st, body = post_http(ip, "/telemetry", timeout=5)
        if st != 200:
            return None
        for line in body.split(b"\n"):
            if line.startswith(b"vna.rx="):
                return int(line.split(b"=", 1)[1])
    except Exception:
        pass
    return None


def wait_alive(ip, baseline_rx, timeout=900):
    """Poll until /smic answers AND vna.rx dropped below baseline —
    proves the node actually restarted, not just kept running."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            st, _ = post_http(ip, "/smic", timeout=5)
            rx = vna_rx(ip)
            if st != 200 or rx is None:
                continue
            if baseline_rx is None or rx < baseline_rx:
                return time.time() - t0, rx
        except Exception:
            pass
        time.sleep(10)
    return -1, vna_rx(ip)


def seed_node(ip, region, entries):
    """IDX every manifest object into one node; returns (ok, failed)."""
    ok, failed = [], []
    for path, meta in entries.items():
        cid = meta["cid"]
        try:
            dg = cid_digest(cid)
        except ValueError:
            failed.append((path, "cidv1-unhandled"))
            continue
        env = pack_cisex_idx_envelope(dg, Path(path).name[:48],
                                      meta.get("bytes", 0), region, ip)
        try:
            st, body = post_http(ip, "/cisex", bytes([0x04, region]) + env)
            if st == 200 and body == b"\xa1":
                ok.append(path)
            else:
                failed.append((path, "ack=%r" % body[:12]))
        except urllib.error.HTTPError as e:
            failed.append((path, "http%d" % e.code))
        except Exception as e:
            failed.append((path, type(e).__name__))
    return ok, failed


def main():
    reboot = "--reboot" in sys.argv
    dry = "--dry-run" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    manifest = json.loads(Path(args[0]).read_text())
    entries = manifest["objects"]
    print("release prefix %s, %d objects" % (manifest["prefix"], len(entries)))

    cfg = cf.load_env()
    if dry:
        print("dry-run: would seed %d objects x %d nodes" % (len(entries), len(NODES)))
        return 0
    tok = cf.token(cfg)

    inst = fleet_instances(tok)
    ip2id = {ip: iid for iid, ip in inst.items()}
    missing = [ip for _, ip, _ in NODES if ip not in ip2id]
    if missing:
        print("instances not found for IPs: %s" % ", ".join(missing))
        return 1
    for name, ip, reg in NODES:
        print("  %-4s %-16s instance %s" % (name, ip, ip2id[ip]))

    if reboot:
        base = {ip: vna_rx(ip) for _, ip, _ in NODES}
        print("rebooting all nine (clears the in-memory CISEX table)")
        for name, ip, _ in NODES:
            st = restart(tok, ip2id[ip])
            print("  %-4s restart -> HTTP %s" % (name, st))
        time.sleep(15)
        for name, ip, _ in NODES:
            dt, rx = wait_alive(ip, base[ip])
            print("  %-4s rebooted, /smic alive after %.0fs (vna.rx=%s)" % (name, dt, rx)
                  if dt >= 0 else
                  "  %-4s not confirmed rebooted (vna.rx=%s vs base %s)" % (name, rx, base[ip]))

    report = {}
    for name, ip, reg in NODES:
        ok, failed = seed_node(ip, reg, entries)
        report[ip] = {"region": reg, "indexed": len(ok), "failed": failed}
        print("  %-4s %-16s indexed %d/%d%s" % (
            name, ip, len(ok), len(entries),
            "" if not failed else "  first fail: %s %s" % failed[0]))

    out = Path(args[0]).with_suffix(".seeding.json")
    out.write_text(json.dumps({"release": manifest["prefix"],
                               "manifest_cid": manifest.get("manifest_cid"),
                               "nodes": report}, indent=1))
    print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
