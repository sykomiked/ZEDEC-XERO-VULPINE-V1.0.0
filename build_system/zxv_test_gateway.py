#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""zxv_test_gateway.py - a local trustless gateway over a block store, for tests.

    python3 build_system/zxv_test_gateway.py BLOCKS PORT [--tamper-over BYTES]

Serves GET /ipfs/<cid>?format=raw from BLOCKS/<cid> (mk_bucket.py --blocks)
on 127.0.0.1:PORT, HTTP/1.1 with keep-alive and Content-Length, which is what
the kernel's fetcher (kernel/src/swarmnet) and tools/zxv-get ask for. Under
QEMU's user network the guest reaches it as http://10.0.2.2:PORT.

--tamper-over BYTES flips the first byte of every block larger than BYTES
(the disc images' 256 KiB leaves, not the small manifest and directory
blocks), so a test can prove that a gateway which lies gets nothing
committed. Binds the loopback only; serves nothing else.
"""

import http.server
import os
import sys


def main():
    if len(sys.argv) not in (3, 5) or (len(sys.argv) == 5 and sys.argv[3] != "--tamper-over"):
        sys.exit(__doc__.strip().splitlines()[2].strip())
    blocks, port = sys.argv[1], int(sys.argv[2])
    over = int(sys.argv[4]) if len(sys.argv) == 5 else None

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            path, _, q = self.path.partition("?")
            cid = path[len("/ipfs/"):]
            f = os.path.join(blocks, cid)
            if not path.startswith("/ipfs/") or q != "format=raw" or "/" in cid or \
                    not cid or not os.path.isfile(f):
                self.send_response(404)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            b = open(f, "rb").read()
            if over is not None and len(b) > over:
                b = bytes([b[0] ^ 1]) + b[1:]
            self.send_response(200)
            self.send_header("Content-Type", "application/vnd.ipld.raw")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def log_message(self, fmt, *a):
            sys.stderr.write("[gateway] " + (fmt % a) + "\n")

    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
