#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""smoke_test.py BINARY — start the app headless, check it end to end, stop it.

Fails unless: it starts, serves the window, its state is valid JSON, every
cycle's allotments add up to exactly the tokens per cycle, cycles settle with
none held, shorthand is read back fully bracketed, and Quit stops it."""
import json
import socket
import subprocess
import sys
import time
import urllib.request


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def get(port, path, data=None):
    req = urllib.request.Request('http://127.0.0.1:%d%s' % (port, path), data=data)
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.read()


def main(binary):
    out = subprocess.run([binary, '--version'], capture_output=True, text=True, timeout=10)
    assert out.returncode == 0 and out.stdout.startswith('zxv-host '), out
    port = free_port()
    proc = subprocess.Popen([binary, '--no-window', '--port', str(port)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        for _ in range(50):
            try:
                html = get(port, '/')
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError('the app never answered')
        assert b'ZXV SWARM' in html, 'window page missing'
        time.sleep(3)                                # let some market cycles run
        s = json.loads(get(port, '/api/state'))
        total = sum(a['re'] + a['mk'] + a['im'] for a in s['agents'])
        assert s['cycle'] > 0, 'no market cycles ran'
        assert total == s['tokens_per_cycle'], 'allotments %d != T %d' % (total, s['tokens_per_cycle'])
        assert s['held'] == 0 and s['settled'] == s['cycle'], 'a cycle failed to settle'
        assert len(s['agents']) >= 1 and s['agents'][0]['name'] == 'Companion'
        ans = get(port, '/api/ask', b'plan(trip) -> verify(costs)').decode()
        assert '(plan(trip) -> verify(costs))' in ans, ans
        get(port, '/api/quit', b'')
        proc.wait(timeout=5)
        print('  smoke test passed: %d agents, %d levels, T = %d, %d cycles settled'
              % (len(s['agents']), s['levels'], s['tokens_per_cycle'], s['settled']))
    finally:
        if proc.poll() is None:
            proc.kill()


if __name__ == '__main__':
    main(sys.argv[1])
