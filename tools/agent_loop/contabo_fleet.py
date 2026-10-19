#!/usr/bin/env python3
# contabo_fleet.py — the nine-server armada through the Contabo API backend.
# Reads the credentials from ~/.config/zxv-deploy.env (mode 600); never
# prints a secret. Read-only by default; every mutating call is explicit.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: contabo_fleet.py list | token-check | get <id>"""
import json
import os
import sys
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

ENV = Path.home() / ".config" / "zxv-deploy.env"
API = "https://api.contabo.com/v1"


def load_env():
    cfg = {}
    for line in ENV.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        cfg[k.strip()] = v.strip().strip('"').strip("'")
    return cfg


def token(cfg):
    body = urllib.parse.urlencode({
        "client_id": cfg["CONTABO_CLIENT_ID"],
        "client_secret": cfg["CONTABO_CLIENT_SECRET"],
        "username": cfg["CONTABO_USER"],
        "password": cfg["CONTABO_PASS"],
        "grant_type": "password",
    }).encode()
    req = urllib.request.Request(
        "https://auth.contabo.com/auth/realms/contabo/protocol/openid-connect/token",
        data=body, headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, timeout=45) as r:
        return json.loads(r.read())["access_token"]


def api_get(tok, path):
    # the API requires a uuid4 in x-request-id (a 400 otherwise)
    req = urllib.request.Request(API + path, headers={
        "Authorization": "Bearer " + tok, "x-request-id": str(uuid.uuid4())})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def main():
    cfg = load_env()
    if not all(k in cfg for k in ("CONTABO_CLIENT_ID", "CONTABO_CLIENT_SECRET",
                                  "CONTABO_USER", "CONTABO_PASS")):
        print("the deploy env is missing the Contabo keys"); return 1
    cmd = sys.argv[1] if len(sys.argv) > 1 else "list"
    tok = token(cfg)
    if cmd == "token-check":
        print("the API token was issued (length %d)" % len(tok)); return 0
    if cmd == "get":
        d = api_get(tok, "/compute/instances/" + sys.argv[2])
        print(json.dumps(d, indent=1)[:2000]); return 0
    d = api_get(tok, "/compute/instances?size=50")
    rows = d.get("data", [])
    print("instances: %d" % len(rows))
    for i in rows:
        print("  %-9s %-18s %-11s %-14s %s" % (
            i.get("instanceId"), (i.get("ipConfig") or {}).get("v4", {}).get("ip", "-"),
            i.get("status", "-"), i.get("region", "-"), i.get("name", "-")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
