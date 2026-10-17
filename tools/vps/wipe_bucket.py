#!/usr/bin/env python3
# wipe_bucket.py — the clean Filebase update: delete every object in the
# bucket (no conflicts from pre-existing primitives), then the release
# publish re-uploads one clean tree. Run AFTER the release seals.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: python3 tools/vps/wipe_bucket.py [--bucket zedec] [--dry-run]

Lists every object (paginated) and deletes it in batches of 1000. The
bucket is then empty: the publish_release.py re-uploads the sealed
release as the only content. --dry-run lists without deleting.
"""
import argparse
import os
import sys
import urllib.parse
import xml.etree.ElementTree as ET

ENV_PATH = os.path.expanduser("~/.config/zxv-deploy.env")
if os.path.exists(ENV_PATH):
    with open(ENV_PATH) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip().strip('"'))

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
from s3_sigv4 import request  # noqa: E402

NS = {"s3": "http://s3.amazonaws.com/doc/2006-03-01/"}


def list_all(bucket):
    keys = []
    marker = ""
    while True:
        q = "list-type=2&max-keys=1000" + ("&continuation-token=" + urllib.parse.quote(marker) if marker else "")
        st, body = request("GET", "/" + bucket, query=q)
        if st != 200:
            raise RuntimeError("list failed: %d %s" % (st, body[:120]))
        root = ET.fromstring(body)
        for c in root.findall("s3:Contents", NS):
            keys.append(c.findtext("s3:Key", "", NS))
        nxt = root.findtext("s3:NextContinuationToken", "", NS)
        if not nxt:
            break
        marker = nxt
    return keys


def delete_batch(bucket, keys):
    xml = ('<?xml version="1.0" encoding="UTF-8"?>'
           '<Delete xmlns="http://s3.amazonaws.com/doc/2006-03-01/">')
    for k in keys:
        xml += "<Object><Key>%s</Key></Object>" % k.replace("&", "&amp;").replace("<", "&lt;")
    xml += "</Delete>"
    st, body = request("POST", "/" + bucket, query="delete", body=xml.encode(),
                       content_type="application/xml")
    return st, body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bucket", default="zedec")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    keys = list_all(args.bucket)
    print("bucket %s: %d objects" % (args.bucket, len(keys)))
    if args.dry_run:
        print("dry-run: nothing deleted")
        return 0
    for i in range(0, len(keys), 1000):
        batch = keys[i:i + 1000]
        st, body = delete_batch(args.bucket, batch)
        if st != 200:
            print("delete batch failed: %d %s" % (st, body[:200]))
            return 1
        print("deleted %d..%d" % (i, i + len(batch)))
    print("bucket empty: ready for the clean publish")
    return 0


if __name__ == "__main__":
    sys.exit(main())