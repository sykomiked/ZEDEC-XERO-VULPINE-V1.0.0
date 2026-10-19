#!/usr/bin/env python3
"""Minimal AWS SigV4 S3 client (stdlib only) for the Filebase deploy path."""
import datetime, hashlib, hmac, os, sys, urllib.request, urllib.error, urllib.parse

ENDPOINT = os.environ["FILEBASE_ENDPOINT"]
AK = os.environ["FILEBASE_ACCESS_KEY"]
SK = os.environ["FILEBASE_SECRET_KEY"]
REGION = "us-east-1"
SERVICE = "s3"


def _hmac(key, msg):
    if not isinstance(msg, bytes):
        msg = msg.encode()
    return hmac.new(key, msg, hashlib.sha256).digest()


def _sigkey(date, region, service, sk):
    k = _hmac(("AWS4" + sk).encode(), date)
    k = _hmac(k, region)
    k = _hmac(k, service)
    return _hmac(k, "aws4_request")


def sign(method, path, query, body=b"", headers=None, content_type=None):
    headers = dict(headers or {})
    now = datetime.datetime.now(datetime.timezone.utc)
    amz = now.strftime("%Y%m%dT%H%M%SZ")
    date = now.strftime("%Y%m%d")
    payload_hash = hashlib.sha256(body).hexdigest()
    hdrs = {
        "host": urllib.parse.urlsplit(ENDPOINT).netloc,
        "x-amz-content-sha256": payload_hash,
        "x-amz-date": amz,
    }
    if content_type:
        hdrs["content-type"] = content_type
    hdrs.update(headers)
    canon_headers = "".join(f"{k.lower()}:{str(v).strip()}\n" for k, v in sorted(hdrs.items()))
    signed = ";".join(k.lower() for k in sorted(hdrs))
    canon = "\n".join([method, path, query, canon_headers, signed, payload_hash])
    scope = f"{date}/{REGION}/{SERVICE}/aws4_request"
    sts = "\n".join(["AWS4-HMAC-SHA256", amz, scope, hashlib.sha256(canon.encode()).hexdigest()])
    sig = hmac.new(_sigkey(date, REGION, SERVICE, SK), sts.encode(), hashlib.sha256).hexdigest()
    hdrs["authorization"] = (
        f"AWS4-HMAC-SHA256 Credential={AK}/{scope}, SignedHeaders={signed}, Signature={sig}"
    )
    url = ENDPOINT + path + ("?" + query if query else "")
    req = urllib.request.Request(url, data=body if body else None, method=method)
    for k, v in hdrs.items():
        req.add_header(k, v)
    return req


def request(method, path, query="", body=b"", headers=None, content_type=None):
    req = sign(method, path, query, body, headers, content_type)
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def list_buckets():
    return request("GET", "/")


def put_object(bucket, key, data, content_type="application/octet-stream", acl=None):
    hdrs = {"x-amz-acl": acl} if acl else {}
    return request("PUT", f"/{bucket}/{key}", body=data, headers=hdrs, content_type=content_type)


def get_object(bucket, key):
    return request("GET", f"/{bucket}/{key}")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "list-buckets":
        st, body = list_buckets()
        print("status", st)
        print(body.decode(errors="replace")[:2000])
    elif cmd == "put":
        bucket, key, path = sys.argv[2], sys.argv[3], sys.argv[4]
        with open(path, "rb") as f:
            data = f.read()
        st, body = put_object(bucket, key, data, acl=sys.argv[5] if len(sys.argv) > 5 else None)
        print("status", st)
        print(body.decode(errors="replace")[:500])
    else:
        print("unknown command")