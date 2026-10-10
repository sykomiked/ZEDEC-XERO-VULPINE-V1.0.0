#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_host_net.py BINARY — the hosted app's peers, update check and
notifications, end to end over real sockets.

Fails unless: without --net the app holds no UDP socket and never contacts
the update gateway by itself; two app instances started with --net lan on
127.0.0.1 (different ports, B given A as --peer) each see the other in
/api/state; the window's Network setting (POST /api/net) turns networking
off and on; a user-requested update check (POST /api/update) contacts only
the configured gateway with a trustless-gateway URL for the built-in bucket,
reports the outcome in /api/state, posts a note to /api/notes and hands it
to the OS notifier (a stand-in notify-send on PATH records its argv)."""
import http.server
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


class App:
    def __init__(self, binary, env, *args):
        self.p = subprocess.Popen([binary, '--no-window', '--port', '0', '--exit-with-parent'] +
                                  list(args), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, env=env)
        self.port = self.token = self.udp = None
        t0 = time.time()
        while time.time() - t0 < 20:
            line = self.p.stdout.readline()
            if not line:
                break
            m = re.match(r'Network: \w+, UDP [0-9.]+:(\d+), node ([0-9a-f]{16})$', line.strip())
            if m:
                self.udp = int(m.group(1))
            m = re.match(r'ZXV-URL: http://127\.0\.0\.1:(\d+)/#token=([0-9a-f]{64})$', line.strip())
            if m:
                self.port, self.token = int(m.group(1)), m.group(2)
                break

    def req(self, method, path, body=b''):
        s = socket.create_connection(('127.0.0.1', self.port), timeout=40)
        head = ('%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nOrigin: http://127.0.0.1:%d\r\n'
                'X-ZXV-Token: %s\r\nContent-Length: %d\r\n\r\n' %
                (method, path, self.port, self.port, self.token, len(body)))
        s.sendall(head.encode() + body)
        data = b''
        while True:
            k = s.recv(65536)
            if not k:
                break
            data += k
        s.close()
        hdr, _, rest = data.partition(b'\r\n\r\n')
        return int(hdr.split(b' ', 2)[1]), rest

    def state(self):
        st, body = self.req('GET', '/api/state')
        return json.loads(body) if st == 200 else {}

    def stop(self):
        if self.p.poll() is None:
            try:
                self.req('POST', '/api/quit')
                self.p.wait(timeout=5)
            except Exception:
                self.p.kill()


def udp_sockets(pid):
    """Linux: how many UDP sockets the process holds (None elsewhere)."""
    try:
        inodes = set()
        for fd in os.listdir('/proc/%d/fd' % pid):
            t = os.readlink('/proc/%d/fd/%s' % (pid, fd))
            if t.startswith('socket:['):
                inodes.add(t[8:-1])
        n = 0
        for f in ('/proc/net/udp', '/proc/net/udp6'):
            if os.path.exists(f):
                for line in open(f).read().splitlines()[1:]:
                    if line.split()[9] in inodes:
                        n += 1
        return n
    except OSError:
        return None


class Gateway(http.server.BaseHTTPRequestHandler):
    seen = []

    def do_GET(self):
        Gateway.seen.append((self.path, self.headers.get('Accept')))
        self.send_response(404)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *a):
        pass


def main(binary):
    home = tempfile.mkdtemp(prefix='zxv-net-')
    fakebin = os.path.join(home, 'bin')
    os.mkdir(fakebin)
    argv_log = os.path.join(home, 'notify-send.argv')
    with open(os.path.join(fakebin, 'notify-send'), 'w') as f:
        f.write('#!/bin/sh\nfor a in "$@"; do printf "%%s\\n" "$a"; done > "%s"\n' % argv_log)
    os.chmod(os.path.join(fakebin, 'notify-send'), 0o755)
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'))
    env.pop('ZXV_TOKEN', None)
    env.pop('DISPLAY', None)
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('DBUS_SESSION_BUS_ADDRESS', None)
    apps = []
    gw = http.server.HTTPServer(('127.0.0.1', 0), Gateway)
    threading.Thread(target=gw.serve_forever, daemon=True).start()
    try:
        # --- off by default
        a0 = App(binary, env, '--models-dir', home)
        apps.append(a0)
        check(a0.port is not None and a0.udp is None, 'started without --net: no network line')
        time.sleep(1.5)
        s = a0.state()
        check(s.get('net', {}).get('mode') == 'off' and not s['net']['bound'], 'state: net off')
        n = udp_sockets(a0.p.pid)
        check(n in (0, None), 'the process holds no UDP socket (%s)' % n)
        check(s['update']['requests'] == 0 and s['update']['checks'] == 0,
              'no update request was made by itself')
        if sys.platform == 'darwin':
            # macOS needs no desktop-session variable: osascript is always there.
            check(s['notes']['os'] == 'osascript', 'macOS: the OS notifier is osascript')
        else:
            check(s['notes']['os'] == 'none', 'no desktop session: the OS notifier is none')
        a0.stop()

        # --- two instances find each other over UDP on 127.0.0.1
        a = App(binary, env, '--models-dir', home, '--net', 'lan', '--net-bind', '127.0.0.1',
                '--net-port', '0')
        apps.append(a)
        check(a.udp is not None, 'A: networking on, UDP port %s' % a.udp)
        b = App(binary, env, '--models-dir', home, '--net', 'lan', '--net-bind', '127.0.0.1',
                '--net-port', '0', '--peer', '127.0.0.1:%d' % (a.udp or 1))
        apps.append(b)
        check(b.udp is not None and b.udp != a.udp, 'B: networking on, UDP port %s' % b.udp)
        sa = sb = {}
        for _ in range(60):
            sa, sb = a.state(), b.state()
            if sa['net']['peers'] >= 1 and sb['net']['peers'] >= 1:
                break
            time.sleep(0.25)
        check(sa['net']['peers'] >= 1 and sb['net']['peers'] >= 1,
              'A and B see each other: peers %d / %d' % (sa['net']['peers'], sb['net']['peers']))
        check(sa['net']['refused'] == 0 and sb['net']['refused'] == 0 and sa['net']['in'] > 0,
              'every datagram verified (in %d / %d)' % (sa['net']['in'], sb['net']['in']))
        check(sa['net']['node_id'] != sb['net']['node_id'], 'distinct node ids')
        n = udp_sockets(a.p.pid)
        check(n in (1, None), 'A holds exactly one UDP socket (%s)' % n)

        st, body = b.req('POST', '/api/net', b'off')
        s = b.state()
        check(st == 200 and s['net']['mode'] == 'off' and not s['net']['bound'],
              'Network setting off: socket closed (%s)' % body.decode())
        st, body = b.req('POST', '/api/net', b'lan')
        s = b.state()
        check(st == 200 and s['net']['mode'] == 'lan' and s['net']['bound'], 'and on again')
        st, body = b.req('POST', '/api/net', b'peer 8.8.8.8:8723')
        check(st == 200 and body == b'refused', 'LAN mode refuses a public peer')
        st, body = b.req('POST', '/api/net', b'peer 127.0.0.1:%d' % a.udp)
        check(st == 200 and body == b'pinged', 'a LAN peer can be added from the window')
        for x in (a, b):
            x.stop()

        # --- update check on request, through the configured gateway only
        genv = dict(env, PATH=fakebin + os.pathsep + env.get('PATH', ''), DISPLAY=':99')
        gurl = 'http://127.0.0.1:%d' % gw.server_address[1]
        c = App(binary, genv, '--models-dir', home, '--update-gateway', gurl)
        apps.append(c)
        time.sleep(1.0)
        s = c.state()
        check(s['update']['requests'] == 0 and not Gateway.seen, 'nothing fetched before asked')
        # macOS always picks /usr/bin/osascript (an absolute path the test
        # cannot replace), so the stand-in notify-send is only checked elsewhere.
        mac = sys.platform == 'darwin'
        want_os = 'osascript' if mac else 'notify-send'
        check(s['notes']['os'] == want_os, 'OS notifier found: %s' % s['notes']['os'])
        st, body = c.req('POST', '/api/update')
        s = c.state()
        u = s['update']
        check(st == 200 and u['checked'] and u['checks'] == 1 and u['requests'] >= 1,
              'user-requested check ran: %s (%d requests)' % (u['status'], u['requests']))
        check(u['status'] == 'network unavailable' and not u['installable'],
              'a gateway without the bucket reports "network unavailable", nothing installable')
        check(bool(Gateway.seen) and all(re.match(r'/ipfs/b[a-z2-7]+\?format=raw$', p) and
                                         acc == 'application/vnd.ipld.raw'
                                         for p, acc in Gateway.seen),
              'only trustless-gateway block requests were made: %s' % Gateway.seen[:2])
        check(any('bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354' in p
                  for p, _ in Gateway.seen), 'for the built-in bucket CID')
        st, body = c.req('GET', '/api/notes')
        notes = json.loads(body)['notes']
        check(any(x['kind'] == 'update' and 'Update check' in x['title'] for x in notes),
              'the outcome is a note in /api/notes')
        for _ in range(0 if mac else 40):
            if os.path.exists(argv_log):
                break
            time.sleep(0.1)
        got = open(argv_log).read().splitlines() if os.path.exists(argv_log) else []
        if mac:
            print('  skip and was pushed to the OS notifier (osascript on macOS is not interceptable here)')
        else:
            check(got[:5] == ['-u', 'normal', '-a', 'ZXV', '--'] and any('Update check' in g for g in got),
                  'and was pushed to the OS notifier as an argv (no shell): %s' % got[:7])
        st, body = c.req('POST', '/api/notes/read')
        check(st == 200 and c.state()['notes']['unread'] == 0, 'notes marked read')
        c.stop()
    finally:
        for x in apps:
            if x.p.poll() is None:
                x.p.kill()
        gw.shutdown()
        shutil.rmtree(home, ignore_errors=True)
    if FAILS:
        sys.exit('host net test failed: ' + '; '.join(FAILS))
    print('  host net test passed')


if __name__ == '__main__':
    main(sys.argv[1])
