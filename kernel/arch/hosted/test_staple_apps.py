#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_staple_apps.py ZXVD — the staple apps' path end to end: start zxvd
headless with a scratch ZXV folder and drive files.* the way the window does,
through POST /api/rpc (zxv_host.c) behind the API guard, then check the
same files on disk.

Fails unless: the page carries the apps (Murmuration, Burrow, Quillon, Glint, Spindle)
and a CSP that lets them show blob: images and media; /api/rpc refuses a
request without the token and a GET; a text file written in chunks (the
editor's begin/add/end) comes back whole, in UTF-8, through chunked reads;
an imported binary round-trips through base64; nothing outside the folder
can be named; the Trash keeps and then removes; and every file landed in
ZXV_FILES_ROOT."""
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_host_api import raw  # noqa: E402 (one HTTP request, exact headers)

FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


def main(binary):
    home = tempfile.mkdtemp(prefix='zxv-apps-')
    root = os.path.join(home, 'ZXV files')
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'),
               ZXV_FILES_ROOT=root)
    env.pop('ZXV_TOKEN', None)
    sock = os.path.join(home, 'run', 'zxvd.sock')
    proc = subprocess.Popen([binary, '--no-window', '--port', '0', '--models-dir', home,
                             '--rpc', sock, '--exit-with-parent'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        url = None
        t0 = time.time()
        while time.time() - t0 < 20:
            line = proc.stdout.readline()
            if not line:
                break
            url = re.match(r'ZXV-URL: http://127\.0\.0\.1:(\d+)/#token=([0-9a-f]{64})$',
                           line.strip())
            if url:
                break
        check(url is not None, 'zxvd prints its ZXV-URL')
        if not url:
            return
        port, token = int(url.group(1)), url.group(2)
        host = ('Host', '127.0.0.1:%d' % port)
        seq = [0]

        def call(method, params=None, tok=token):
            seq[0] += 1
            body = json.dumps({'jsonrpc': '2.0', 'id': seq[0], 'method': method,
                               'params': params or {}}).encode()
            st, _, data = raw(port, 'POST', '/api/rpc', [host, ('X-ZXV-Token', tok),
                                                        ('Content-Type', 'application/json')],
                              body)
            return st, (json.loads(data) if st == 200 else data)

        def ok(method, params=None):
            st, r = call(method, params)
            if st != 200 or 'error' in r:
                check(False, '%s %s: %s %s' % (method, params, st, r))
                return {}
            return r['result']

        st, hdr, page = raw(port, 'GET', '/', [host])
        page = page.decode('utf-8', 'replace')
        check(st == 200 and all('data-app="%s"' % a in page
                                for a in ('swarm', 'files', 'text', 'photos', 'console')),
              'the window lists the staple apps')
        check("img-src 'self' data: blob:" in hdr and 'media-src blob:' in hdr,
              'the CSP allows blob: images and media')

        st, _ = call('files.root', tok='0' * 64)
        check(st == 403, '/api/rpc without the right token is refused (%d)' % st)
        st, _, _ = raw(port, 'GET', '/api/rpc', [host, ('X-ZXV-Token', token)])
        check(st == 405, 'GET /api/rpc is refused (%d)' % st)

        r = ok('files.root')
        check(r.get('root') == root and os.path.isdir(root), 'the ZXV folder is ZXV_FILES_ROOT')
        check(ok('files.list').get('entries') == [], 'a new folder is empty')

        # the editor's save: a long text in begin/add/end chunks
        text = ('Ünïcødé line ✓ 🦊\n' * 1500) + 'end'
        size = len(text.encode())
        pieces = [text[i:i + 3000] for i in range(0, len(text), 3000)]
        check(ok('files.mkdir', {'path': 'Notes'}).get('path') == 'Notes', 'mkdir')
        for i, p in enumerate(pieces):
            mode = 'begin' if i == 0 else 'end' if i == len(pieces) - 1 else 'add'
            r = ok('files.write', {'path': 'Notes/long.md', 'text': p, 'mode': mode})
            if i == 0:
                check(not os.path.exists(os.path.join(root, 'Notes', 'long.md')),
                      'nothing lands before "end"')
        check(r.get('size') == size and r.get('done') is True, 'the last chunk reports the size')
        got, off = [], 0
        while True:
            c = ok('files.read', {'path': 'Notes/long.md', 'offset': off})
            got.append(c.get('text', ''))
            if c.get('eof', True):
                break
            off = c['next']
        check(''.join(got) == text, 'chunked reads give the text back whole (%d bytes)' % size)
        with open(os.path.join(root, 'Notes', 'long.md'), encoding='utf-8') as f:
            check(f.read() == text, 'the file on disk is the text')

        # an import: binary through base64
        blob = bytes(range(256)) * 50
        ok('files.write', {'path': 'pic.png', 'base64': base64.b64encode(blob[:9000]).decode(),
                           'mode': 'begin', 'exclusive': True})
        ok('files.write', {'path': 'pic.png', 'base64': base64.b64encode(blob[9000:]).decode(),
                           'mode': 'end', 'exclusive': True})
        back, off = b'', 0
        while True:
            c = ok('files.read', {'path': 'pic.png', 'offset': off, 'encoding': 'base64'})
            back += base64.b64decode(c.get('base64', ''))
            if c.get('eof', True):
                break
            off = c['next']
        check(back == blob, 'a binary file round-trips through base64')
        st, r = call('files.read', {'path': 'pic.png'})
        check(r.get('error', {}).get('code') == -32012, 'a text read of binary is refused')
        st, r = call('files.write', {'path': 'pic.png', 'base64': 'AA==', 'exclusive': True})
        check(r.get('error', {}).get('code') == -32012, 'an import never overwrites')

        # nothing outside the folder
        for bad in ('../escape.txt', '/etc/passwd', 'a/../../x', '.ssh/id', 'C:\\x'):
            st, r = call('files.write', {'path': bad, 'text': 'x'})
            check('error' in r, 'refused: %s' % bad)
        os.symlink(home, os.path.join(root, 'out'))
        st, r = call('files.list', {'path': 'out'})
        check(r.get('error', {}).get('code') == -32012, 'a link out of the folder is refused')
        os.unlink(os.path.join(root, 'out'))
        check(not os.path.exists(os.path.join(home, 'escape.txt')), 'nothing was written outside')

        # Trash
        r = ok('files.trash', {'path': 'Notes'})
        check(r.get('trashed') == '.Trash/Notes' and
              os.path.isfile(os.path.join(root, '.Trash', 'Notes', 'long.md')), 'Trash keeps')
        check([e['name'] for e in ok('files.list').get('entries', [])] == ['pic.png'],
              'the listing hides the Trash')
        check(ok('files.empty_trash').get('removed') == 1 and
              not os.listdir(os.path.join(root, '.Trash')), 'Empty Trash removes')

        st, _, _ = raw(port, 'POST', '/api/quit', [host, ('X-ZXV-Token', token)])
        check(st == 200, 'quit')
        proc.wait(timeout=20)
    finally:
        if proc.poll() is None:
            proc.kill()
        shutil.rmtree(home, ignore_errors=True)
    if FAILS:
        print('[FAIL] test_staple_apps: %d failed' % len(FAILS))
        sys.exit(1)
    print('[PASS] test_staple_apps')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
