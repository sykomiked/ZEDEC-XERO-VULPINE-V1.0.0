#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_host_api.py BINARY [GGUF] — start the hosted app headless and check it
end to end over real sockets, including the attacks the API guard stops
(zxv_http_guard.h). Replaces build_system/smoke_test.py, which predates the
token. With GGUF, the file is installed in a fresh models folder first and
the model slot is checked too.

Fails unless: the app starts and prints its ZXV-URL; the page loads and
carries the token plumbing; every API call without the right token, with a
foreign Host (DNS rebinding), a foreign or null Origin, or a cross-site
Sec-Fetch-Site is refused and changes nothing; the swarm's invariants hold
(allotments sum to T, every cycle settles); shorthand is read back; and only
a correctly authorised Quit stops it."""
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


def raw(port, method, path, headers, body=b''):
    """One HTTP request with exactly these headers (no Host is added)."""
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    head = '%s %s HTTP/1.1\r\n' % (method, path)
    for k, v in headers:
        head += '%s: %s\r\n' % (k, v)
    if body or method == 'POST':
        head += 'Content-Length: %d\r\n' % len(body)
    s.sendall(head.encode() + b'\r\n' + body)
    data = b''
    while True:
        k = s.recv(65536)
        if not k:
            break
        data += k
    s.close()
    status = int(data.split(b' ', 2)[1]) if data.startswith(b'HTTP/1.1 ') else 0
    hdr, _, rest = data.partition(b'\r\n\r\n')
    return status, hdr.decode('latin-1'), rest


def main(binary, gguf=None):
    out = subprocess.run([binary, '--version'], capture_output=True, text=True, timeout=10)
    check(out.returncode == 0 and out.stdout.startswith('zxv-host '), '--version')

    home = tempfile.mkdtemp(prefix='zxv-test-')
    models = os.path.join(home, 'models')
    os.mkdir(models)
    if gguf:
        shutil.copy(gguf, os.path.join(models, 'tiny-test.gguf'))
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'))
    env.pop('ZXV_TOKEN', None)
    proc = subprocess.Popen([binary, '--no-window', '--port', '0', '--models-dir', models,
                             '--exit-with-parent'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        url = None
        t0 = time.time()
        while time.time() - t0 < 10:
            line = proc.stdout.readline()
            if not line:
                break
            m = re.match(r'ZXV-URL: http://127\.0\.0\.1:(\d+)/#token=([0-9a-f]{64})$', line.strip())
            if m:
                url = m
                break
        check(url is not None, 'prints its ZXV-URL with a 64-hex token')
        if not url:
            return
        port, token = int(url.group(1)), url.group(2)
        host = ('Host', '127.0.0.1:%d' % port)
        auth = ('X-ZXV-Token', token)
        same = ('Origin', 'http://127.0.0.1:%d' % port)

        st, hdr, html = raw(port, 'GET', '/', [host, ('Sec-Fetch-Site', 'none')])
        check(st == 200 and b'ZXV SWARM' in html, 'page loads without a token')
        check(b'X-ZXV-Token' in html and b'#&]token=' in html, 'page carries the token plumbing')
        check(token.encode() not in html, 'page does not contain the token')
        check('X-Frame-Options: DENY' in hdr and "frame-ancestors 'none'" in hdr, 'page refuses framing')

        # --- refused: none of these may run, and none may stop the app
        bad = [
            ('no token', [host], 403),
            ('wrong token', [host, ('X-ZXV-Token', '0' * 64)], 403),
            ('token as query', [host], 403),
            ('DNS-rebound Host', [('Host', 'rebind.evil.example:%d' % port), auth], 403),
            ('Host on another port', [('Host', '127.0.0.1:%d' % (port + 1)), auth], 403),
            ('no Host', [auth], 403),
            ('foreign Origin', [host, ('Origin', 'https://evil.example'), auth], 403),
            ('null Origin', [host, ('Origin', 'null'), auth], 403),
            ('cross-site fetch', [host, ('Sec-Fetch-Site', 'cross-site'), auth], 403),
            ('same-site fetch', [host, ('Sec-Fetch-Site', 'same-site'), auth], 403),
        ]
        for name, h, want in bad:
            path = '/api/quit?token=' + token if name == 'token as query' else '/api/quit'
            st, _, _ = raw(port, 'POST', path, h)
            check(st == want, 'POST /api/quit refused: %s (%d)' % (name, st))
        # the exact request a hostile web page's fetch() would send
        st, _, _ = raw(port, 'POST', '/api/quit',
                       [host, ('Origin', 'https://evil.example'), ('Sec-Fetch-Site', 'cross-site'),
                        ('Content-Type', 'text/plain;charset=UTF-8')], b'x')
        check(st == 403, 'cross-site simple POST from a web page refused')
        st, _, _ = raw(port, 'OPTIONS', '/api/quit',
                       [host, ('Origin', 'https://evil.example'),
                        ('Access-Control-Request-Method', 'POST'),
                        ('Access-Control-Request-Headers', 'x-zxv-token')])
        check(st == 405, 'CORS preflight never granted (%d)' % st)
        st, _, _ = raw(port, 'GET', '/api/state', [('Host', 'rebind.evil.example:%d' % port)])
        check(st == 403, 'rebinding read of /api/state refused')
        st, _, _ = raw(port, 'POST', '/api/ask', [host, auth, ('Transfer-Encoding', 'chunked')],
                       b'')
        check(st == 403, 'chunked body refused')
        st, _, _ = raw(port, 'GET', '/api/quit', [host, auth])
        check(st == 405, 'GET /api/quit is not a quit')
        check(proc.poll() is None, 'the app is still running after all refused requests')

        # --- allowed: the window's own requests
        time.sleep(3)  # let some market cycles run
        st, _, body = raw(port, 'GET', '/api/state', [host, auth, ('Sec-Fetch-Site', 'same-origin')])
        check(st == 200, 'state with the token')
        s = json.loads(body)
        total = sum(a['re'] + a['mk'] + a['im'] for a in s['agents'])
        check(s['cycle'] > 0, 'market cycles ran')
        check(total == s['tokens_per_cycle'], 'allotments %d == T %d' % (total, s['tokens_per_cycle']))
        check(s['held'] == 0 and s['settled'] == s['cycle'], 'every cycle settled')
        check(len(s['agents']) >= 1 and s['agents'][0]['name'] == 'Companion', 'companion first')
        check('model' in s, 'state reports the model slot')
        st, _, ans = raw(port, 'POST', '/api/ask', [('Host', 'localhost:%d' % port),
                                                    ('Origin', 'http://localhost:%d' % port), auth],
                         b'plan(trip) -> verify(costs)')
        check(st == 200 and b'(plan(trip) -> verify(costs))' in ans, 'shorthand read back')
        if gguf:
            m = s['model']
            check(m['loaded'] and m['tokenizer_ok'] and m['arch'] == 'qwen2' and m['vocab'] > 1000,
                  'model slot loaded the GGUF and its tokenizer: %s' % m['status'])
            st, _, ans = raw(port, 'POST', '/api/ask', [host, same, auth], b'hello there, swarm')
            check(st == 200 and (b'tokens' in ans or m['can_generate']),
                  'ask goes through the model slot: %s' % ans[:120].decode('utf-8', 'replace'))
        else:
            check(not s['model']['loaded'], 'no model is a normal state')

        st, _, _ = raw(port, 'POST', '/api/quit', [host, same, auth])
        check(st == 200, 'authorised quit accepted')
        proc.wait(timeout=5)
        check(proc.returncode == 0, 'the app stopped cleanly')

        # --exit-with-parent: closing stdin stops a second instance
        p2 = subprocess.Popen([binary, '--no-window', '--port', '0', '--models-dir', models,
                               '--exit-with-parent'], stdin=subprocess.PIPE,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        time.sleep(0.5)
        p2.stdin.close()
        try:
            p2.wait(timeout=5)
            check(True, 'stops when its parent closes stdin')
        except subprocess.TimeoutExpired:
            p2.kill()
            check(False, 'stops when its parent closes stdin')

        # a token handed in by the native shell is used; a malformed one is not
        chosen = 'ab' * 32
        for tok, want in ((chosen, True), ('not-a-token', False)):
            p3 = subprocess.Popen([binary, '--no-window', '--port', '0', '--models-dir', models],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                  env=dict(env, ZXV_TOKEN=tok))
            line = ''
            for _ in range(20):
                line = p3.stdout.readline()
                if line.startswith('ZXV-URL:'):
                    break
            m = re.search(r':(\d+)/#token=([0-9a-f]{64})', line)
            check(m is not None and ((m.group(2) == chosen) == want),
                  'ZXV_TOKEN %s' % ('used' if want else 'malformed, replaced by a fresh one'))
            if m:
                raw(int(m.group(1)), 'POST', '/api/quit',
                    [('Host', '127.0.0.1:' + m.group(1)), ('X-ZXV-Token', m.group(2))])
            try:
                p3.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p3.kill()
    finally:
        if proc.poll() is None:
            proc.kill()
        shutil.rmtree(home, ignore_errors=True)
    if FAILS:
        sys.exit('host API test failed: ' + '; '.join(FAILS))
    print('  host API test passed')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
