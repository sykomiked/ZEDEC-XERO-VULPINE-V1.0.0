#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""s3_multipart_upload.py — High-throughput parallel S3 single/multipart uploader for Filebase.

Uploads files of any size (from bytes to multi-GB archives) to Filebase S3
(https://s3.filebase.io, bucket 'zxv') using AWS Signature Version 4 and
parallel 64 MiB multipart chunks for files > 64 MiB, then queries HEAD to
retrieve the canonical IPFS CID (`x-amz-meta-cid`).
"""
import concurrent.futures
import datetime
import hashlib
import hmac
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

def _load_env_file() -> dict:
    p = Path.home() / ".config" / "zxv-deploy.env"
    d = {}
    if p.exists():
        for line in p.read_text().splitlines():
            if "=" in line and not line.strip().startswith("#"):
                k, v = line.strip().split("=", 1)
                d[k.strip()] = v.strip()
    return d


_ENV = _load_env_file()
ACCESS_KEY = os.environ.get("FILEBASE_ACCESS_KEY") or _ENV.get("FILEBASE_ACCESS_KEY", "")
SECRET_KEY = os.environ.get("FILEBASE_SECRET_KEY") or _ENV.get("FILEBASE_SECRET_KEY", "")
ENDPOINT   = os.environ.get("FILEBASE_ENDPOINT") or _ENV.get("FILEBASE_ENDPOINT", "https://s3.filebase.io")
REGION     = "us-east-1"
BUCKET     = os.environ.get("FILEBASE_BUCKET", "zxv")
PART_SIZE  = 64 * 1024 * 1024  # 64 MiB parts


def sign_key(key: str, date_stamp: str, region: str, service: str) -> bytes:
    k_date = hmac.new(("AWS4" + key).encode(), date_stamp.encode(), hashlib.sha256).digest()
    k_region = hmac.new(k_date, region.encode(), hashlib.sha256).digest()
    k_service = hmac.new(k_region, service.encode(), hashlib.sha256).digest()
    return hmac.new(k_service, b"aws4_request", hashlib.sha256).digest()


def s3_request(method: str, key: str, query: str = "", data: bytes = b"",
               extra_headers: dict | None = None, timeout: int = 300):
    now = datetime.datetime.now(datetime.timezone.utc)
    amz_date = now.strftime("%Y%m%dT%H%M%SZ")
    date_stamp = now.strftime("%Y%m%d")
    payload_hash = hashlib.sha256(data).hexdigest()
    uri = f"/{BUCKET}/{urllib.parse.quote(key, safe='/~.-_')}"
    headers = {
        "host": "s3.filebase.io",
        "x-amz-content-sha256": payload_hash,
        "x-amz-date": amz_date,
    }
    if extra_headers:
        for hk, hv in extra_headers.items():
            headers[hk.lower()] = hv
    signed_headers = ";".join(sorted(headers.keys()))
    canon_headers = "".join(f"{k}:{headers[k]}\n" for k in sorted(headers.keys()))
    canon_req = f"{method}\n{uri}\n{query}\n{canon_headers}\n{signed_headers}\n{payload_hash}"
    scope = f"{date_stamp}/{REGION}/s3/aws4_request"
    sts = f"AWS4-HMAC-SHA256\n{amz_date}\n{scope}\n{hashlib.sha256(canon_req.encode()).hexdigest()}"
    sig = hmac.new(sign_key(SECRET_KEY, date_stamp, REGION, "s3"), sts.encode(), hashlib.sha256).hexdigest()
    headers["Authorization"] = (
        f"AWS4-HMAC-SHA256 Credential={ACCESS_KEY}/{scope}, "
        f"SignedHeaders={signed_headers}, Signature={sig}"
    )
    url = ENDPOINT + uri + (f"?{query}" if query else "")
    req = urllib.request.Request(url, data=data if method in ("PUT", "POST") else None,
                                 method=method, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        hdrs = {k.lower(): v for k, v in r.headers.items()}
        return r.status, hdrs, r.read()


def upload_file(key: str, filepath: Path, content_type: str = "application/octet-stream") -> dict:
    sz = filepath.stat().st_size
    t0 = time.time()
    if sz <= PART_SIZE:
        data = filepath.read_bytes()
        for attempt in range(4):
            try:
                st, hdrs, _ = s3_request("PUT", key, "", data, {"content-type": content_type})
                cid = hdrs.get("x-amz-meta-cid") or hdrs.get("X-Amz-Meta-Cid") or ""
                if not cid:
                    _, h2, _ = s3_request("HEAD", key)
                    cid = h2.get("x-amz-meta-cid") or h2.get("X-Amz-Meta-Cid") or ""
                return {"key": key, "bytes": sz, "cid": cid, "seconds": round(time.time() - t0, 2)}
            except Exception:
                if attempt == 3:
                    raise
                time.sleep(1.5 * (attempt + 1))

    # Multipart upload for > 64 MiB
    _, _, init_xml = s3_request("POST", key, "uploads=", b"", {"content-type": content_type})
    m = re.search(rb"<UploadId>([^<]+)</UploadId>", init_xml)
    if not m:
        raise RuntimeError(f"No UploadId in {init_xml!r}")
    upload_id = m.group(1).decode()

    num_parts = (sz + PART_SIZE - 1) // PART_SIZE

    def upload_part(part_num: int):
        off = (part_num - 1) * PART_SIZE
        length = min(PART_SIZE, sz - off)
        with open(filepath, "rb") as f:
            f.seek(off)
            chunk = f.read(length)
        q = f"partNumber={part_num}&uploadId={urllib.parse.quote(upload_id, safe='')}"
        for attempt in range(5):
            try:
                _, phdrs, _ = s3_request("PUT", key, q, chunk, timeout=300)
                etag = phdrs.get("etag", "")
                return part_num, etag
            except Exception:
                if attempt == 4:
                    raise
                time.sleep(2.0 * (attempt + 1))

    parts = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for pnum, etag in ex.map(upload_part, range(1, num_parts + 1)):
            parts.append((pnum, etag))

    parts.sort(key=lambda x: x[0])
    xml_parts = ["<CompleteMultipartUpload>"]
    for pnum, etag in parts:
        xml_parts.append(f"<Part><PartNumber>{pnum}</PartNumber><ETag>{etag}</ETag></Part>")
    xml_parts.append("</CompleteMultipartUpload>")
    comp_body = "".join(xml_parts).encode()

    q_comp = f"uploadId={urllib.parse.quote(upload_id, safe='')}"
    _, chdrs, cbody = s3_request("POST", key, q_comp, comp_body, {"content-type": "application/xml"}, timeout=300)
    cid = chdrs.get("x-amz-meta-cid") or chdrs.get("X-Amz-Meta-Cid") or ""
    if not cid:
        _, h2, _ = s3_request("HEAD", key)
        cid = h2.get("x-amz-meta-cid") or h2.get("X-Amz-Meta-Cid") or ""
    return {"key": key, "bytes": sz, "cid": cid, "parts": num_parts, "seconds": round(time.time() - t0, 2)}


if __name__ == "__main__":
    key_arg = sys.argv[1]
    path_arg = Path(sys.argv[2])
    res = upload_file(key_arg, path_arg)
    print(json.dumps(res))
