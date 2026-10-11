#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_rpc_client.py ZXVD ZXV_RPC_CALL — the native shell's side of zxvd.

Starts zxvd exactly as ZXVApp.m does (--no-window --port 0
--exit-with-parent, --models-dir, --rpc in a 0700 run folder, ZXV_TOKEN in
the environment) and drives it with zxv-rpc-call, the command-line wrapper
round macos/zxv_rpc_client.c, which ZXVApp.m compiles in unchanged. Fails
unless: the window URL still carries the shell's own token; the client gets
exact replies and errors; the unread count reads from notes.list; a missing
socket, a refused path and a multi-line request fail cleanly; zxvd.shutdown
from the client stops zxvd; and closing stdin (the shell's fallback) still
stops a second zxvd."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

FAILS = []
TOKEN = 'ab' * 32


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


def start(zxvd, home, sock):
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'), ZXV_TOKEN=TOKEN)
    models = os.path.join(home, 'models')
    os.makedirs(models, exist_ok=True)
    p = subprocess.Popen([zxvd, '--no-window', '--port', '0', '--exit-with-parent',
                          '--models-dir', models, '--rpc', sock],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, env=env, cwd=home)
    lines = {}
    t0 = time.time()
    while time.time() - t0 < 20 and len(lines) < 2:
        line = p.stdout.readline()
        if not line:
            break
        for k in ('ZXV-URL: ', 'ZXVD-RPC: '):
            if line.startswith(k):
                lines[k] = line[len(k):].strip()
    return p, lines


def call(cli, *args):
    r = subprocess.run([cli] + list(args), capture_output=True, text=True, timeout=20)
    return r.returncode, r.stdout.strip()


def main(zxvd, cli):
    # in /tmp: under macOS's long $TMPDIR the socket path below would pass
    # zxvd's 103-byte limit
    home = tempfile.mkdtemp(prefix='zxv-shell-', dir='/tmp')
    sock = os.path.join(home, 'Application Support', 'ZXV', 'run', 'zxvd.sock')
    os.makedirs(os.path.dirname(os.path.dirname(sock)), 0o700)
    p, lines = start(zxvd, home, sock)
    try:
        check(lines.get('ZXVD-RPC: ') == sock, 'zxvd announces the shell\'s socket path')
        url = lines.get('ZXV-URL: ', '')
        check(url.startswith('http://127.0.0.1:') and url.endswith('#token=' + TOKEN),
              'the window is still served with the shell\'s own token')

        rc, out = call(cli, sock, '{"jsonrpc":"2.0","id":7,"method":"zxvd.version"}')
        r = json.loads(out) if rc == 0 else {}
        check(r.get('id') == 7 and r.get('result', {}).get('protocol') == 'zxvd-rpc/1',
              'zxvd.version through the client: %s' % out[:80])
        rc, out = call(cli, sock, '{"jsonrpc":"2.0","id":"s","method":"swarm.state"}')
        r = json.loads(out) if rc == 0 else {}
        check(r.get('id') == 's' and isinstance(r.get('result'), dict) and len(out) > 1000,
              'swarm.state, a long reply, arrives whole (%d bytes)' % len(out))
        rc, out = call(cli, sock, '{"jsonrpc":"2.0","id":8,"method":"no.such"}')
        check(rc == 0 and json.loads(out).get('error', {}).get('code') == -32601,
              'an error reply is passed through')
        rc, out = call(cli, '--unread', sock)
        check(rc == 0 and out.isdigit(), 'unread count from notes.list (%s)' % out)
        rc, out = call(cli, sock, '{"jsonrpc":"2.0","id":9,\n"method":"zxvd.version"}')
        check(rc == 1 and out == '', 'a request with a newline is never sent')
        rc, _ = call(cli, os.path.join(home, 'nothing.sock'), '{}')
        check(rc == 1, 'a missing socket fails cleanly')
        rc, _ = call(cli, '/' + 'x' * 200, '{}')
        check(rc == 1, 'an over-long socket path fails cleanly')

        rc, out = call(cli, sock, '{"jsonrpc":"2.0","id":10,"method":"zxvd.shutdown"}')
        check(rc == 0 and json.loads(out).get('result', {}).get('stopping') is True,
              'zxvd.shutdown through the client')
        p.wait(timeout=10)
        check(p.returncode == 0, 'zxvd exits cleanly after shutdown')
        check(not os.path.exists(sock), 'and removes its socket')
        rc, _ = call(cli, sock, '{"jsonrpc":"2.0","id":11,"method":"zxvd.version"}')
        check(rc == 1, 'a stopped zxvd: the client fails cleanly')

        # the shell's fallback: closing stdin stops zxvd too
        p, lines = start(zxvd, home, sock)
        check(lines.get('ZXVD-RPC: ') == sock, 'a second zxvd starts on the same path')
        p.stdin.close()
        p.wait(timeout=10)
        check(p.returncode == 0 and not os.path.exists(sock),
              'closing stdin stops it and removes the socket')
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        shutil.rmtree(home, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2])
    except Exception as e:
        check(False, 'unexpected: %r' % (e,))
    if FAILS:
        print('[FAIL] shell client: %d check(s) failed' % len(FAILS))
        sys.exit(1)
    print('[PASS] native shell client against zxvd')
