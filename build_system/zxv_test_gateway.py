#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""zxv_test_gateway.py - a local trustless gateway over a block store, for tests.

    python3 build_system/zxv_test_gateway.py BLOCKS PORT [--tamper-over BYTES]
        [--down-after BYTES [--down-for SECONDS]]

Serves GET /ipfs/<cid>?format=raw from BLOCKS/<cid> (mk_bucket.py --blocks)
on 127.0.0.1:PORT, HTTP/1.1 with keep-alive and Content-Length, which is what
the kernel's fetcher (kernel/src/swarmnet) and tools/zxv-get ask for. Under
QEMU's user network the guest reaches it as http://10.0.2.2:PORT.

--tamper-over BYTES flips the first byte of every block larger than BYTES
(the disc images' 256 KiB leaves, not the small manifest and directory
blocks), so a test can prove that a gateway which lies gets nothing
committed.

--down-after BYTES answers 503 to every request once it has served that many
bytes, for --down-for SECONDS (default: for good), then serves again: a
gateway that drops out mid-download, so a test can prove the fetch waits for
it, or resumes on a later boot. Binds the loopback only; serves nothing else.
"""

import argparse
import http.server
import os
import sys
import threading
import time


def main():
    ap = argparse.ArgumentParser(usage=__doc__.strip().splitlines()[2].strip())
    ap.add_argument("blocks")
    ap.add_argument("port", type=int)
    ap.add_argument("--tamper-over", type=int)
    ap.add_argument("--down-after", type=int)
    ap.add_argument("--down-for", type=float)
    a = ap.parse_args()
    root = os.path.realpath(a.blocks)
    state = {"served": 0, "down_at": None}
    lock = threading.Lock()

    def down():
        with lock:
            if a.down_after is None or state["served"] < a.down_after:
                return False
            if state["down_at"] is None:
                state["down_at"] = time.monotonic()
                sys.stderr.write("[gateway] down after %d bytes\n" % state["served"])
            if a.down_for is not None and time.monotonic() - state["down_at"] >= a.down_for:
                a.down_after = None
                sys.stderr.write("[gateway] back up\n")
                return False
            return True

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def reply(self, code, b=b""):
            self.send_response(code)
            if code == 200:
                self.send_header("Content-Type", "application/vnd.ipld.raw")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def do_GET(self):
            path, _, q = self.path.partition("?")
            cid = path[len("/ipfs/"):]
            # a CID is letters and digits only; the block must sit directly in BLOCKS
            if not path.startswith("/ipfs/") or q != "format=raw" or not cid.isalnum() or \
                    not cid.isascii():
                return self.reply(404)
            f = os.path.realpath(os.path.join(root, cid))
            if os.path.dirname(f) != root or not os.path.isfile(f):
                return self.reply(404)
            if down():
                return self.reply(503)
            b = open(f, "rb").read()
            if a.tamper_over is not None and len(b) > a.tamper_over:
                b = bytes([b[0] ^ 1]) + b[1:]
            with lock:
                state["served"] += len(b)
            self.reply(200, b)

        def log_message(self, fmt, *args):
            sys.stderr.write("[gateway] " + (fmt % args) + "\n")

    http.server.ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
