#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_zxvd_irc.py ZXVD — the IRC gateway (zxvd_irc.h) in a real zxvd, with
OpenSSL (Python's ssl module) as the client, so the gateway's TLS 1.3 server
is checked against an independent implementation. Fails unless: nothing
listens until the irc feature is on; features.set turns it on and off; the
key, certificate and password are written 0600 beside the socket; OpenSSL
verifies the certificate pinned from irc-cert.pem for localhost and
127.0.0.1 and refuses it without the pin; a plain-text client gets no IRC; a
client registers with the password, asks #swarm and gets a signed, tagged
answer (N without a model). POSIX only."""
import json
import os
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
import time

FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


def rpc(sock, method, params=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(30)
    s.connect(sock)
    req = {'jsonrpc': '2.0', 'id': 1, 'method': method}
    if params is not None:
        req['params'] = params
    s.sendall((json.dumps(req) + '\n').encode())
    buf = b''
    while b'\n' not in buf:
        k = s.recv(65536)
        if not k:
            break
        buf += k
    s.close()
    return json.loads(buf)


def start(binary, home, extra):
    sock = os.path.join(home, 'run', 'zxvd.sock')
    proc = subprocess.Popen([binary, '--rpc', sock, '--exit-with-parent'] + extra,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True,
                            env=dict(os.environ, HOME=home))
    irc = None
    t0 = time.time()
    while time.time() - t0 < 30:
        line = proc.stdout.readline()
        if not line:
            break
        if line.startswith('ZXVD-IRC: TLS 127.0.0.1:'):
            irc = int(line.split()[2].split(':')[1])
        if line.startswith('ZXVD-RPC: '):
            break
    return proc, sock, irc


def stop(proc, sock):
    try:
        rpc(sock, 'zxvd.shutdown')
    except OSError:
        pass
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()


def status(sock, want_listening, timeout=10):
    t0 = time.time()
    while True:
        st = rpc(sock, 'irc.status')['result']
        if st['listening'] == want_listening or time.time() - t0 > timeout:
            return st
        time.sleep(0.05)


class Client:
    def __init__(self, port, cafile, host='localhost'):
        ctx = ssl.create_default_context(cafile=cafile)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_3
        raw = socket.create_connection(('127.0.0.1', port), timeout=10)
        self.s = ctx.wrap_socket(raw, server_hostname=host)
        self.buf = b''

    def send(self, line):
        self.s.sendall((line + '\r\n').encode())

    def until(self, needle, timeout=15):
        """lines up to and including the first one containing needle"""
        t0 = time.time()
        self.s.settimeout(1)
        while time.time() - t0 < timeout:
            text = self.buf.decode('utf-8', 'replace')
            if needle in text:
                i = text.index(needle)
                j = text.find('\n', i)
                j = len(text) if j < 0 else j + 1
                self.buf = text[j:].encode()
                return text[:j]
            try:
                k = self.s.recv(65536)
            except socket.timeout:
                continue
            if not k:
                break
            self.buf += k
        return None

    def close(self):
        self.s.close()


def main(binary):
    home = tempfile.mkdtemp(prefix='zxvd-irc-')
    proc, sock, irc = start(binary, home, ['--irc-port', '0'])
    try:
        check(irc is None, 'no IRC line while the feature is off')
        st = rpc(sock, 'irc.status')['result']
        check(st['on'] is False and st['listening'] is False and st['port'] == 0,
              'irc.status: off, not listening')
        r = rpc(sock, 'irc.bridge', {'on': True})
        check(r.get('error', {}).get('code') == -32012, 'irc.bridge needs the switch')
        run = os.path.join(home, 'run')
        for f in ('irc.key', 'irc.der', 'irc-cert.pem', 'irc.pass', 'irc.seed'):
            p = os.path.join(run, f)
            check(os.path.exists(p) and stat.S_IMODE(os.stat(p).st_mode) == 0o600,
                  '%s beside the socket, 0600' % f)
        rpc(sock, 'features.set', {'feature': 'irc', 'enabled': True})
        st = status(sock, True)
        check(st['listening'] is True and st['port'] > 0, 'features.set turns it on')
        port, cafile, pw = st['port'], st['cert_file'], st['password']
        check(cafile == os.path.join(run, 'irc-cert.pem'), 'the certificate file is named')

        # OpenSSL refuses it without the pin
        try:
            Client(port, None)
            check(False, 'OpenSSL refuses an unpinned self-signed certificate')
        except ssl.SSLCertVerificationError:
            check(True, 'OpenSSL refuses an unpinned self-signed certificate')
        # and verifies it for both names with the pin
        for host in ('localhost', '127.0.0.1'):
            c = Client(port, cafile, host)
            check(c.s.version() == 'TLSv1.3' and c.s.cipher()[0] == 'TLS_CHACHA20_POLY1305_SHA256',
                  'OpenSSL: TLS 1.3, ChaCha20-Poly1305, verified for %s' % host)
            c.close()

        # plain text gets no IRC
        raw = socket.create_connection(('127.0.0.1', port), timeout=5)
        raw.sendall(b'NICK x\r\nUSER x 0 * :x\r\n')
        got = b''
        try:
            while True:
                k = raw.recv(4096)
                if not k:
                    break
                got += k
        except (socket.timeout, ConnectionResetError):
            pass
        raw.close()
        check(b'001' not in got and b'NOTICE' not in got, 'a plain-text client gets no IRC')

        c = Client(port, cafile)
        c.send('CAP LS 302')
        c.send('PASS ' + pw)
        c.send('NICK owl')
        c.send('USER owl 0 * :Owl')
        c.send('CAP REQ :message-tags server-time')
        c.send('CAP END')
        check(c.until(' 001 ') is not None, 'registered with the password')
        c.send('JOIN #swarm')
        names = c.until(' 366 ')
        check(names is not None and 'swarm' in names, '#swarm lists the swarm')
        c.send('PRIVMSG #swarm :[T] hello swarm')
        ans = c.until(':swarm!', timeout=60)
        check(ans is not None, 'the swarm answers')
        if ans:
            line = ans.strip().splitlines()[-1]
            check('PRIVMSG #swarm :[N] ' in line and 'zxv/truth=N' in line and 'time=' in line,
                  'without a model the answer is N, with its tags: %s' % line[:160])
            mid = line.split('msgid=')[1][:32]
            rec = rpc(sock, 'irc.record', {'msgid': mid}).get('result', {})
            check(rec.get('nick') == 'swarm' and rec.get('truth') == 'N' and
                  len(rec.get('signature', '')) == 2 * 4627,
                  'irc.record holds the signed answer (ML-DSA-87)')
        st = rpc(sock, 'irc.status')['result']
        check(st['clients'] == 1 and st['delivered'] >= 2, 'one client, its messages delivered')

        rpc(sock, 'features.set', {'feature': 'irc', 'enabled': False})
        st = status(sock, False)
        check(st['listening'] is False, 'features.set turns it off')
        check(c.until('\x01never', timeout=3) is None, 'and the client is closed')
        c.close()
    finally:
        stop(proc, sock)

    # --enable irc listens from the start and says where
    proc, sock, irc = start(binary, home, ['--enable', 'irc', '--irc-port', '0'])
    try:
        check(irc is not None and irc > 0, 'ZXVD-IRC names the port with --enable irc')
        st = rpc(sock, 'irc.status')['result']
        check(st['port'] == irc and st['password'] == pw, 'the same password after a restart')
    finally:
        stop(proc, sock)
    if FAILS:
        print('[FAIL] test_zxvd_irc.py: %d failures' % len(FAILS))
        return 1
    print('[PASS] test_zxvd_irc.py (OpenSSL %s)' % ssl.OPENSSL_VERSION)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
