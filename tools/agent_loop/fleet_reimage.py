#!/usr/bin/env python3
# fleet_reimage.py — reinstall one or more of the nine-server armada from a
# custom image, through the Contabo API backend. The image must be in state
# "downloaded". The prior image stays in the catalogue, so the operation is
# reversible by reimaging back.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: fleet_reimage.py <imageId> <instanceId> [instanceId ...]"""
import json
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import contabo_fleet as cf  # noqa: E402


def post(tok, path, body, method="PUT"):
    req = urllib.request.Request(cf.API + path, data=json.dumps(body).encode(),
                                 method=method,
                                 headers={"Authorization": "Bearer " + tok,
                                          "Content-Type": "application/json",
                                          "x-request-id": str(uuid.uuid4())})
    try:
        with urllib.request.urlopen(req, timeout=90) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()[:300]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image_id, ids = sys.argv[1], sys.argv[2:]
    cfg = cf.load_env()
    tok = cf.token(cfg)

    st, d = cf.api_get(tok, "/compute/images/" + image_id), None
    if st.get("data", [{}])[0].get("status") != "downloaded":
        print("the image is not in state 'downloaded': %s"
              % st.get("data", [{}])[0].get("status"))
        return 1

    print("the image: %s (%s)" % (st["data"][0].get("name"), image_id))
    for iid in ids:
        st, body = post(tok, "/compute/instances/" + iid, {"imageId": image_id})
        print("  instance %-10s -> HTTP %s %s" % (
            iid, st, "" if st in (200, 201) else body))
        time.sleep(2)

    print("\nthe reinstall is asynchronous: poll the instance status and its")
    print("serial console; the audit trail is GET /compute/instances/{id}/audits")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
