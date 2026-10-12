#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_model_fetch.py — zxv-model-fetch.sh and the shipped models.manifest.

Serves fake models from a local HTTP server and runs the fetcher on
throwaway manifests. Fails unless: a line with no CID ("-") is fetched from
its mirror and installed with its hash checked; a mirror that sends the
wrong bytes is discarded and the next one is used; a line whose every
source is wrong installs nothing and exits 1; an installed, verified model
is kept without a download; malformed mirrors (plain HTTP to another host,
a shell pattern, a query string) are skipped; and every entry in the real
macos/models.manifest is well formed, licensed for commercial use, and
names at least one source."""
import hashlib
import http.server
import os
import re
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
FETCH = os.path.join(HERE, 'zxv-model-fetch.sh')
MANIFEST = os.path.join(HERE, 'models.manifest')
FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


def run(manifest_text, dest, work):
    m = os.path.join(work, 'm.manifest')
    with open(m, 'w') as f:
        f.write(manifest_text)
    env = dict(os.environ, ZXV_IPFS_GATEWAYS='http://127.0.0.1:9')
    p = subprocess.run(['sh', FETCH, m, dest], env=env, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout + p.stderr


def main():
    work = tempfile.mkdtemp(prefix='zxv-fetch-')
    srv_dir = os.path.join(work, 'srv')
    os.makedirs(srv_dir)
    good = b'GGUF' + os.urandom(4096)
    with open(os.path.join(srv_dir, 'good.gguf'), 'wb') as f:
        f.write(good)
    with open(os.path.join(srv_dir, 'bad.gguf'), 'wb') as f:
        f.write(b'GGUF' + os.urandom(4096))
    h = hashlib.sha256(good).hexdigest()
    n = len(good)

    handler = lambda *a, **k: Quiet(*a, directory=srv_dir, **k)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = 'http://127.0.0.1:%d' % srv.server_address[1]

    print('mirror-only line (CID "-")')
    d1 = os.path.join(work, 'd1')
    rc, log = run('m.gguf %s %d - Apache-2.0 %s/good.gguf\n' % (h, n, base), d1, work)
    got = open(os.path.join(d1, 'm.gguf'), 'rb').read() if os.path.exists(os.path.join(d1, 'm.gguf')) else b''
    check(rc == 0 and got == good, 'installed from the mirror, hash checked (rc %d)' % rc)
    check(not any(x.endswith('.part') for x in os.listdir(d1)), 'no .part file left behind')

    print('kept when already installed')
    rc, log = run('m.gguf %s %d - Apache-2.0 %s/missing.gguf\n' % (h, n, base), d1, work)
    check(rc == 0 and 'already installed and verified' in log, 'no download for a verified model')

    print('a wrong file is discarded, the next mirror is used')
    d2 = os.path.join(work, 'd2')
    rc, log = run('m.gguf %s %d - Apache-2.0 %s/bad.gguf %s/good.gguf\n' % (h, n, base, base), d2, work)
    check(rc == 0 and 'does not match the manifest' in log, 'bad bytes discarded, good mirror installed')
    check(open(os.path.join(d2, 'm.gguf'), 'rb').read() == good, 'installed file is the good one')

    print('every source wrong')
    d3 = os.path.join(work, 'd3')
    rc, log = run('m.gguf %s %d - Apache-2.0 %s/bad.gguf\n' % (h, n, base), d3, work)
    check(rc == 1 and not os.path.exists(os.path.join(d3, 'm.gguf')), 'nothing installed, exit 1')
    check(os.listdir(d3) == [], 'no .part file left behind')

    print('malformed mirrors are skipped')
    d4 = os.path.join(work, 'd4')
    bad = ['http://example.com/good.gguf', base + '/*.gguf', base + '/good.gguf?x=1',
           'ftp://127.0.0.1/good.gguf', 'https://exa mple.com/x']
    for u in bad:
        rc, log = run('m.gguf %s %d - Apache-2.0 %s\n' % (h, n, u), d4, work)
        check(rc == 1 and 'skipping bad mirror' in log and not os.path.exists(os.path.join(d4, 'm.gguf')),
              'refused %r' % u)
    rc, log = run('m.gguf %s %d notacid Apache-2.0 %s/good.gguf\n' % (h, n, base), d4, work)
    check(rc == 1 and 'malformed manifest line' in log, 'a bad CID field skips the line')

    print('the shipped manifest')
    commercial = {'Apache-2.0', 'MIT', 'BSD-3-Clause', 'BSD-2-Clause'}
    entries = 0
    for line in open(MANIFEST):
        f = line.split()
        if not f or f[0].startswith('#'):
            continue
        entries += 1
        name = f[0]
        ok = (len(f) >= 5 and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,120}\.gguf', f[0])
              and re.fullmatch(r'[0-9a-f]{64}', f[1]) and re.fullmatch(r'[0-9]{1,13}', f[2])
              and re.fullmatch(r'-|baf[a-z2-7]{20,120}|Qm[1-9A-HJ-NP-Za-km-z]{44}', f[3]))
        check(bool(ok), '%s: well formed' % name)
        if not ok:
            continue
        check(f[4] in commercial, '%s: licence %s allows commercial use' % (name, f[4]))
        mirrors = f[5:]
        check(f[3] != '-' or mirrors, '%s: has a CID or a mirror' % name)
        for u in mirrors:
            check(re.fullmatch(r'https://[A-Za-z0-9.-]+(:[0-9]{1,5})?/[A-Za-z0-9._~/%+=-]*', u) is not None,
                  '%s: mirror is plain HTTPS: %s' % (name, u))
            check('/resolve/main/' not in u, '%s: mirror pins a commit, not a branch' % name)
    check(entries >= 1, 'the manifest names at least one model (%d)' % entries)

    srv.shutdown()
    if FAILS:
        print('%d check(s) failed' % len(FAILS))
        sys.exit(1)
    print('[PASS] model fetcher')


if __name__ == '__main__':
    main()
