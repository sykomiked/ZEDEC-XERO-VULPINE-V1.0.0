#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_zxvd_spec.py ZXVD — api/zxvd.openrpc.json and the daemon agree: the
methods zxvd serves (rpc.discover) are exactly the spec's, so SDKs generated
from the spec never call a method that does not exist or miss one."""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def main(binary):
    spec = json.load(open(os.path.join(ROOT, 'api', 'zxvd.openrpc.json')))
    want = sorted(m['name'] for m in spec['methods'])
    home = tempfile.mkdtemp(prefix='zxvd-spec-')
    sock = os.path.join(home, 'run', 'zxvd.sock')
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'))
    p = subprocess.Popen([binary, '--rpc', sock, '--models-dir', home], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        t0 = time.time()
        while not os.path.exists(sock) and time.time() - t0 < 20:
            time.sleep(0.05)
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(10)
        s.connect(sock)
        s.sendall(b'{"jsonrpc":"2.0","id":1,"method":"rpc.discover"}\n')
        buf = b''
        while b'\n' not in buf:
            buf += s.recv(65536)
        got = sorted(json.loads(buf)['result']['methods'])
        s.sendall(b'{"jsonrpc":"2.0","id":2,"method":"zxvd.shutdown"}\n')
        s.close()
        p.wait(timeout=10)
    finally:
        if p.poll() is None:
            p.kill()
    if got != want:
        print('[FAIL] spec and daemon differ: only in spec %s, only in zxvd %s' %
              (sorted(set(want) - set(got)), sorted(set(got) - set(want))))
        sys.exit(1)
    print('[PASS] api/zxvd.openrpc.json lists exactly the %d methods zxvd serves' % len(got))


if __name__ == '__main__':
    main(sys.argv[1])
