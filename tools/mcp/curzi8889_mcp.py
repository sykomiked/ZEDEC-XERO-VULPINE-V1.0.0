# curzi8889-mcp — the Model Context Protocol server for the 36n9/9n63
# planes. Adding this package to an agent's mcp.json gives every agent
# in that framework native access to the seed banks, the content index
# and the node surfaces — no custom integration code.
#
# Tools exposed:
#   seed_bank_slice  digest -> bank/len/offset (+bytes when they fit)
#   cisex_search     keyword -> raw 168-byte UBH-168 envelopes
#   telemetry        the node counters (key=value lines)
#   spaces           the shared spaces directory
#   uri_resolve      36n9:// -> the Filebase gateway fallback URL
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Run:  python -m curzi8889_mcp [--url NODE_URL]

The node URL defaults to http://127.0.0.1:8799 (a local zxvd) — point it
at any PoP node of the armada.
"""
import argparse
import json
import sys
import urllib.request


def post(url, path, body=b"", timeout=10):
    req = urllib.request.Request(url + path, data=body, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def hex_to_bytes(h):
    return bytes.fromhex(h)


class ZxvMCP:
    def __init__(self, url):
        self.url = url

    def seed_bank_slice(self, digest_hex):
        d = hex_to_bytes(digest_hex)
        if len(d) != 32:
            return {"error": "digest must be 32 bytes (64 hex chars)"}
        b = post(self.url, "/bank", d)
        status = b[0]
        bank = int.from_bytes(b[1:3], "big")
        ln = int.from_bytes(b[3:7], "big")
        off = int.from_bytes(b[7:11], "big")
        return {"status": "hit" if status == 0 else "miss", "bank": bank,
                "len": ln, "offset": off,
                "bytes": b[11:].hex() if len(b) > 11 else ""}

    def cisex_search(self, query):
        q = query.encode()
        body = b"\x02\x00" + bytes([len(q)]) + q
        b = post(self.url, "/cisex", body)
        n = b[1] if len(b) > 1 and b[0] == 0xB0 else 0
        names = []
        for i in range(n):
            e = b[2 + i * 168:2 + (i + 1) * 168]
            if len(e) >= 64:
                names.append(e[32:64].split(b"\x00")[0].decode(errors="replace"))
        return {"count": n, "envelopes": [e.hex() for e in
                [b[2 + i * 168:2 + (i + 1) * 168] for i in range(n)]],
                "names": names}

    def telemetry(self):
        return post(self.url, "/telemetry").decode(errors="replace")

    def spaces(self):
        b = post(self.url, "/spaces")
        n = b[0]
        names = []
        at = 2
        for _ in range(n):
            nl = b[at]
            names.append(b[at + 1:at + 1 + nl].decode(errors="replace"))
            at += 1 + nl + 4
        return {"count": n, "spaces": names}

    def uri_resolve(self, uri):
        if uri.startswith("36n9://ipfs/"):
            return {"gateway": "https://ipfs.filebase.io/ipfs/" + uri[len("36n9://ipfs/"):]}
        if uri.startswith("36n9://bank."):
            return {"note": "resolve against a node: seed_bank_slice (the bank holds the slice)"}
        return {"error": "unrecognized 36n9 URI"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8799")
    args = ap.parse_args()
    mcp = ZxvMCP(args.url)
    # the MCP stdio protocol: one JSON request per line, one response per line
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            name = req.get("tool")
            args_in = req.get("arguments", {})
            fn = getattr(mcp, name, None)
            if not fn:
                out = {"error": "unknown tool: %s" % name}
            else:
                out = fn(**args_in)
            print(json.dumps({"id": req.get("id"), "result": out}))
        except Exception as e:
            print(json.dumps({"id": req.get("id"), "error": str(e)}))
        sys.stdout.flush()


if __name__ == "__main__":
    main()