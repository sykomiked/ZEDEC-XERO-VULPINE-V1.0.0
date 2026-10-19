#!/usr/bin/env python3
# fleet_sentinel.py — the armada's intelligence node service.
#
# Polls every node's /telemetry and /smic on an interval, appends the full
# key=value state as JSONL (the careful log), and raises alerts when the
# fleet's behaviour deviates: silence (a node or a channel going quiet),
# noise (rejection storms, sync floods), attack signatures (forged-sig,
# bad-proof-of-work or binding rejects surging), quarantine events, and
# pin-coverage drift of the release manifest.
#
# Baselines are rolling per node per metric (deque of the last BASE_N
# samples); an alert fires when a counter's delta exceeds BASE_SIG sigma
# or a hard floor rule trips. Alerts and samples share one append-only log.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: fleet_sentinel.py <release-manifest.json> [--interval SECS] [--logdir DIR]"""
import json
import math
import sys
import time
import urllib.request
import urllib.error
import concurrent.futures
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from armada_mesh_daemon import NODES  # noqa: E402
from fleet_reseed import cid_digest  # noqa: E402

INTERVAL = 30.0
BASE_N = 120          # samples per baseline window
BASE_SIG = 6.0        # sigma for rate anomalies
SILENCE_AFTER = 4     # consecutive dead polls before alerting
RATE_KEYS = ("vna.rx", "vna.tx", "cisex.sync_in", "cisex.sync_out",
             "vna.rej", "vna.refused", "ipfs.bytes")
ATK_KEYS = ("rej.sig", "rej.pow", "rej.binding", "rej.stale",
            "porter.rejected", "rej.parse")


def post(ip, path, data=b"", timeout=8.0):
    req = urllib.request.Request(
        f"http://{ip}{path}", data=data, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, r.read()


def telemetry(ip):
    try:
        _, body = post(ip, "/telemetry")
        return dict(l.split("=", 1) for l in body.decode(errors="replace").splitlines()
                    if "=" in l)
    except Exception as e:
        return {"_err": type(e).__name__}


class Baseline:
    def __init__(self):
        self.w = deque(maxlen=BASE_N)

    def push(self, v):
        self.w.append(v)

    def sigma(self, v):
        if len(self.w) < 12:
            return 0.0
        m = sum(self.w) / len(self.w)
        sd = math.sqrt(sum((x - m) ** 2 for x in self.w) / len(self.w)) or 1.0
        return (v - m) / sd


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    interval = INTERVAL
    logdir = Path("dist/sentinel")
    for i, a in enumerate(sys.argv):
        if a == "--interval":
            interval = float(sys.argv[i + 1])
        if a == "--logdir":
            logdir = Path(sys.argv[i + 1])
    if not args:
        print(__doc__)
        return 2
    manifest = json.loads(Path(args[0]).read_text())
    release_dgs = {p: cid_digest(m["cid"]) for p, m in manifest["objects"].items()
                   if isinstance(m, dict) and "cid" in m}
    release_dgs["manifest"] = cid_digest(manifest["manifest_cid"])
    logdir.mkdir(parents=True, exist_ok=True)
    logf = logdir / ("fleet-%s.jsonl" % time.strftime("%Y%m%d"))

    base = {ip: {k: Baseline() for k in RATE_KEYS} for _, ip, _ in NODES}
    prev = {ip: {} for _, ip, _ in NODES}
    silent = {ip: 0 for _, ip, _ in NODES}

    def log(rec):
        rec["t"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        with logf.open("a") as f:
            f.write(json.dumps(rec) + "\n")

    def alert(node, ip, kind, detail):
        line = f"ALERT {node} {ip} {kind}: {detail}"
        print(line, flush=True)
        log({"type": "alert", "node": node, "ip": ip, "kind": kind, "detail": detail})

    print(f"sentinel up — {len(NODES)} nodes, {len(release_dgs)} release digests, "
          f"log {logf}", flush=True)
    cycle = 0
    while True:
        cycle += 1
        with concurrent.futures.ThreadPoolExecutor(len(NODES)) as ex:
            tels = dict(zip([n for n, _, _ in NODES],
                            ex.map(lambda nd: telemetry(nd[1]), NODES)))

        for name, ip, reg in NODES:
            tel = tels[name]
            if "_err" in tel:
                silent[ip] += 1
                log({"type": "sample", "node": name, "ip": ip, "err": tel["_err"]})
                if silent[ip] == SILENCE_AFTER:
                    alert(name, ip, "SILENCE",
                          f"no telemetry for {silent[ip] * interval:.0f}s")
                continue
            if silent[ip] >= SILENCE_AFTER:
                alert(name, ip, "RECOVERED", "telemetry resumed")
            silent[ip] = 0
            log({"type": "sample", "node": name, "ip": ip, "kv": tel})

            for k in RATE_KEYS:
                if k in tel and k in prev[ip]:
                    try:
                        delta = float(tel[k]) - float(prev[ip][k])
                    except ValueError:
                        continue
                    s = base[ip][k].sigma(delta)
                    base[ip][k].push(delta)
                    if s > BASE_SIG and delta > 0:
                        alert(name, ip, "NOISE",
                              f"{k} delta {delta:.0f} = {s:.1f}σ")
            # hard rules
            try:
                if int(tel.get("smic.quar", 0)) > int(prev[ip].get("smic.quar", 0)):
                    alert(name, ip, "QUARANTINE", "smic.quar incremented")
                if int(tel.get("vna.sessions", 0)) == 0 and int(tel.get("vna.up", 0)) == 1:
                    alert(name, ip, "SILENCE", "vna up but zero sessions")
                for k in ATK_KEYS:
                    if k in tel and k in prev[ip]:
                        d = float(tel[k]) - float(prev[ip][k])
                        if d >= 50:
                            alert(name, ip, "ATTACK",
                                  f"{k} +{d:.0f} this cycle (forged/spam traffic)")
                chg_unc = int(tel.get("chg.uncertain", 0))
                chg_dec = int(tel.get("chg.decided", 0))
                if chg_dec == 0 and chg_unc > 10000 and int(tel.get("chg.alive", 0)):
                    alert(name, ip, "CHIGLET", "judge alive but deciding nothing — evidence stream may be starving")
            except ValueError:
                pass
            prev[ip] = tel

        # every 10th cycle: pin coverage — DQ every release digest on every node
        if cycle % 10 == 1:
            cov = {}
            for name, ip, reg in NODES:
                hits = 0
                for p, dg in release_dgs.items():
                    try:
                        st, body = post(ip, "/cisex", bytes([0x03, reg]) + dg, timeout=6)
                        if st == 200 and body[:1] == b"\xb1":
                            hits += 1
                    except Exception:
                        pass
                cov[name] = hits
                if hits < len(release_dgs):
                    alert(name, ip, "PIN-DRIFT",
                          f"{hits}/{len(release_dgs)} release digests answer DQ")
            print(f"[cycle {cycle}] pin coverage: " +
                  " ".join(f"{n}={c}/{len(release_dgs)}" for n, c in cov.items()),
                  flush=True)
        time.sleep(interval)


if __name__ == "__main__":
    raise SystemExit(main())
