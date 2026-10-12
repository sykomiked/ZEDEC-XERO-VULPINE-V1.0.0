#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_zxvd_phone.py ZXVD — the phone link (zxvd_phone.h) end to end.

A phone client written from the spec alone, with its own pure-Python X25519
(RFC 7748), ChaCha20-Poly1305 (RFC 8439) and HKDF (RFC 5869), so the daemon's
C crypto is checked against an independent implementation, not against
itself. Fails unless: the link is off by default and phone.link turns it on
and off; pairing
returns a link and stores it 0600 beside the socket; a paired phone completes
the handshake and the server proves it knows the key; the views' read methods
answer through the link; shutdown, phone.*, peer.serve and notifications are
refused from a phone, and money methods unless the pairing allows posting; a
wrong key, an unknown id, a tampered frame and an over-long frame each close
the connection; revoking closes the phone's connections and stops it
reconnecting; and pairings survive a restart. POSIX only (L1)."""
import hashlib
import hmac
import json
import os
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse

FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


# ---------- RFC 7748 X25519 ----------
P = 2 ** 255 - 19


def x25519(k, u):
    k = bytearray(k)
    k[0] &= 248
    k[31] &= 127
    k[31] |= 64
    k = int.from_bytes(k, 'little')
    u = int.from_bytes(u, 'little') & ((1 << 255) - 1)
    x1, x2, z2, x3, z3, swap = u, 1, 0, u, 1, 0
    for t in reversed(range(255)):
        kt = (k >> t) & 1
        swap ^= kt
        if swap:
            x2, x3, z2, z3 = x3, x2, z3, z2
        swap = kt
        a, b = (x2 + z2) % P, (x2 - z2) % P
        aa, bb = a * a % P, b * b % P
        e = (aa - bb) % P
        c, d = (x3 + z3) % P, (x3 - z3) % P
        da, cb = d * a % P, c * b % P
        x3, z3 = (da + cb) ** 2 % P, x1 * (da - cb) ** 2 % P
        x2, z2 = aa * bb % P, e * (aa + 121665 * e) % P
    if swap:
        x2, z2 = x3, z3
    return (x2 * pow(z2, P - 2, P) % P).to_bytes(32, 'little')


# ---------- RFC 8439 ChaCha20-Poly1305 ----------
def _rotl(v, c):
    return ((v << c) & 0xffffffff) | (v >> (32 - c))


def _chacha_block(key, counter, nonce):
    s = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574] + list(struct.unpack('<8I', key)) + \
        [counter] + list(struct.unpack('<3I', nonce))
    w = s[:]

    def qr(a, b, c, d):
        w[a] = (w[a] + w[b]) & 0xffffffff; w[d] = _rotl(w[d] ^ w[a], 16)
        w[c] = (w[c] + w[d]) & 0xffffffff; w[b] = _rotl(w[b] ^ w[c], 12)
        w[a] = (w[a] + w[b]) & 0xffffffff; w[d] = _rotl(w[d] ^ w[a], 8)
        w[c] = (w[c] + w[d]) & 0xffffffff; w[b] = _rotl(w[b] ^ w[c], 7)
    for _ in range(10):
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
    return struct.pack('<16I', *[(w[i] + s[i]) & 0xffffffff for i in range(16)])


def _chacha_xor(key, counter, nonce, data):
    out = bytearray()
    for i in range(0, len(data), 64):
        ks = _chacha_block(key, counter + i // 64, nonce)
        out += bytes(a ^ b for a, b in zip(data[i:i + 64], ks))
    return bytes(out)


def _poly1305(key, msg):
    r = int.from_bytes(key[:16], 'little') & 0x0ffffffc0ffffffc0ffffffc0fffffff
    s = int.from_bytes(key[16:], 'little')
    acc, p = 0, (1 << 130) - 5
    for i in range(0, len(msg), 16):
        n = int.from_bytes(msg[i:i + 16] + b'\x01', 'little')
        acc = (acc + n) * r % p
    return ((acc + s) & ((1 << 128) - 1)).to_bytes(16, 'little')


def _mac_data(aad, ct):
    pad = lambda b: b + b'\x00' * (-len(b) % 16)
    return pad(aad) + pad(ct) + struct.pack('<QQ', len(aad), len(ct))


def seal(key, nonce, aad, pt):
    otk = _chacha_block(key, 0, nonce)[:32]
    ct = _chacha_xor(key, 1, nonce, pt)
    return ct, _poly1305(otk, _mac_data(aad, ct))


def open_(key, nonce, aad, ct, tag):
    otk = _chacha_block(key, 0, nonce)[:32]
    if not hmac.compare_digest(_poly1305(otk, _mac_data(aad, ct)), tag):
        return None
    return _chacha_xor(key, 1, nonce, ct)


def hkdf(salt, ikm, info, n):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out, t, i = b'', b'', 1
    while len(out) < n:
        t = hmac.new(prk, t + info + bytes([i]), hashlib.sha256).digest()
        out += t
        i += 1
    return out[:n]


def self_test():
    # RFC 7748 5.2 and RFC 8439 2.8.2
    k = bytes.fromhex('a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4')
    u = bytes.fromhex('e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c')
    assert x25519(k, u).hex() == 'c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552'
    key = bytes(range(0x80, 0xa0))
    nonce = bytes.fromhex('070000004041424344454647')
    aad = bytes.fromhex('50515253c0c1c2c3c4c5c6c7')
    pt = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip " \
         b"for the future, sunscreen would be it."
    ct, tag = seal(key, nonce, aad, pt)
    assert tag.hex() == '1ae10b594f09e26a7e902ecbd0600691', tag.hex()
    assert open_(key, nonce, aad, ct, tag) == pt


# ---------- the phone client (zxvd_phone.h L2, L3) ----------
def parse_link(link):
    assert link.startswith('zxv-phone:1?'), link
    q = urllib.parse.parse_qs(link.split('?', 1)[1])
    return {'host': q['h'][0], 'port': int(q['p'][0]), 'id': bytes.fromhex(q['id'][0]),
            'key': bytes.fromhex(q['k'][0]), 'name': q['n'][0]}


class Phone:
    def __init__(self, host, port, pid, key):
        self.s = socket.create_connection((host, port), timeout=10)
        self.buf = b''
        self.seq_out = self.seq_in = 0
        self.next_id = 1
        ec = os.urandom(32)
        epub = x25519(ec, (9).to_bytes(32, 'little'))
        self.s.sendall(b'ZXP1' + pid + epub)
        rep = self.recv_n(64)
        es = rep[:32]
        dh = x25519(ec, es)
        th = hashlib.sha256(b'zxv-phone/1' + pid + epub + es).digest()
        okm = hkdf(key, dh + th, b'zxv-phone/1 keys', 96)
        self.k_out, self.k_in, kconf = okm[:32], okm[32:64], okm[64:]
        want = hmac.new(kconf, b'server confirm' + th, hashlib.sha256).digest()
        self.confirmed = hmac.compare_digest(want, rep[32:])

    def recv_n(self, n):
        while len(self.buf) < n:
            k = self.s.recv(65536)
            if not k:
                raise ConnectionError('closed')
            self.buf += k
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send_frame(self, pt, tamper=False):
        hdr = struct.pack('>I', len(pt))
        ct, tag = seal(self.k_out, b'\0' * 4 + struct.pack('>Q', self.seq_out), hdr, pt)
        self.seq_out += 1
        if tamper:
            ct = bytes([ct[0] ^ 1]) + ct[1:]
        self.s.sendall(hdr + ct + tag)

    def recv_frame(self):
        hdr = self.recv_n(4)
        n = struct.unpack('>I', hdr)[0]
        body = self.recv_n(n + 16)
        pt = open_(self.k_in, b'\0' * 4 + struct.pack('>Q', self.seq_in), hdr, body[:n], body[n:])
        self.seq_in += 1
        if pt is None:
            raise ValueError('bad frame from zxvd')
        return json.loads(pt)

    def call(self, method, params=None):
        rid = self.next_id
        self.next_id += 1
        req = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            req['params'] = params
        self.send_frame(json.dumps(req).encode())
        r = self.recv_frame()
        assert r.get('id') == rid, r
        return r

    def closed(self):
        """True when zxvd has closed the connection (EOF or reset)."""
        try:
            self.s.settimeout(5)
            return self.s.recv(1) == b''
        except (ConnectionError, OSError):
            return True

    def close(self):
        self.s.close()


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
    port = None
    t0 = time.time()
    while time.time() - t0 < 30:
        line = proc.stdout.readline()
        if not line:
            break
        if line.startswith('ZXVD-PHONE: TCP '):
            port = int(line.split()[2])
        if line.startswith('ZXVD-RPC: '):
            break
    return proc, sock, port


def stop(proc, sock):
    try:
        rpc(sock, 'zxvd.shutdown')
    except OSError:
        pass
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()


def err_code(r):
    return r.get('error', {}).get('code')


def main(binary):
    self_test()
    print('  ok   reference X25519 and ChaCha20-Poly1305 match the RFC vectors')
    home = tempfile.mkdtemp(prefix='zxvd-phone-')

    proc, sock, port = start(binary, home, [])
    try:
        check(port is None, 'no phone link without --phone-link')
        check(rpc(sock, 'phone.list').get('result', {}).get('on') is False,
              'phone.list says the link is off')
        r = rpc(sock, 'phone.pair', {'name': 'x'})
        check(err_code(r) == -32602 and 'phone.link' in r['error']['message'],
              'phone.pair asks for the link first')
        on = rpc(sock, 'phone.link', {'on': True, 'port': 0, 'bind': '127.0.0.1'})['result']
        check(on['on'] is True and on['port'] > 0, 'phone.link turns the link on')
        r = rpc(sock, 'phone.pair', {'name': 'Watch'})['result']
        L0 = parse_link(r['link'])
        check(r['port'] == on['port'], 'the pairing link carries the live port')
        ph = Phone('127.0.0.1', on['port'], L0['id'], L0['key'])
        check(ph.confirmed and 'result' in ph.call('zxvd.version'), 'a phone connects')
        off = rpc(sock, 'phone.link', {'on': False})['result']
        check(off == {'on': False, 'port': 0}, 'phone.link turns it off')
        check(ph.closed(), 'turning the link off closes phones')
        try:
            Phone('127.0.0.1', on['port'], L0['id'], L0['key'])
            check(False, 'nothing listens once the link is off')
        except (ConnectionError, OSError):
            check(True, 'nothing listens once the link is off')
        r = rpc(sock, 'phone.link', {'on': True, 'bind': '8.8.8.8', 'port': 0})
        check(err_code(r) == -32602, 'a public bind address is refused')
        check(rpc(sock, 'phone.revoke', {'phone_id': L0['id'].hex()}).get('result', {}).get(
            'revoked'), 'pairings can be revoked while the link is off')
    finally:
        stop(proc, sock)

    link_args = ['--phone-link', '--phone-port', '0', '--phone-bind', '127.0.0.1']
    proc, sock, port = start(binary, home, link_args)
    try:
        check(port and port > 0, 'zxvd announces the phone port')
        check(err_code(rpc(sock, 'phone.pair', {'name': 'bad\nname'})) == -32602,
              'a name with a control character is refused')
        r = rpc(sock, 'phone.pair', {'name': 'Pixel 9'})
        res = r.get('result', {})
        check(res.get('port') == port and res.get('allow_post') is False and
              res.get('link', '').startswith('zxv-phone:1?'), 'phone.pair returns a link')
        L = parse_link(res['link'])
        check(L['name'] == 'Pixel 9' and L['id'].hex() == res['phone_id'] and len(L['key']) == 32,
              'the link carries id, a 32-byte key and the name')
        pf = os.path.join(home, 'run', 'phones')
        check(stat.S_IMODE(os.stat(pf).st_mode) == 0o600, 'the pairings file is 0600')
        lst = rpc(sock, 'phone.list')['result']
        check([p['name'] for p in lst['phones']] == ['Pixel 9'] and lst['port'] == port,
              'phone.list lists the pairing')
        check(L['key'].hex() not in json.dumps(lst), 'phone.list never shows the key')

        ph = Phone('127.0.0.1', port, L['id'], L['key'])
        check(ph.confirmed, 'handshake: zxvd proves it holds the key')
        v = ph.call('zxvd.version')
        check(v.get('result', {}).get('protocol') == 'zxvd-rpc/1', 'zxvd.version over the link')
        for m in ('swarm.state', 'model.info', 'notes.list', 'ledger.accounts', 'ledger.check',
                  'swarm.earnings', 'peer.status', 'rpc.discover'):
            r = ph.call(m)
            check('result' in r, '%s answers over the link' % m)
        for m in ('zxvd.shutdown', 'phone.list', 'peer.serve'):
            check(err_code(ph.call(m)) == -32012, '%s is refused from a phone' % m)
        r = ph.call('ledger.post', {'from': '1', 'to': '2', 'amount_minor': 1, 'reference': 'x'})
        check(err_code(r) == -32012, 'ledger.post is refused without allow_post')
        check(err_code(ph.call('net.command', {'command': 'off'})) == -32012,
              'net.command is refused without allow_post')
        ph.send_frame(b'{"jsonrpc":"2.0","method":"zxvd.shutdown"}')
        r = ph.recv_frame()
        check(err_code(r) == -32600, 'a notification from a phone is refused, not run')
        check(rpc(sock, 'zxvd.version').get('result') is not None, 'zxvd is still running')
        conn = rpc(sock, 'phone.list')['result']['phones'][0]['connected']
        check(conn == 1, 'phone.list counts the open connection')

        ph.send_frame(b'{"jsonrpc":"2.0","id":9,"method":"zxvd.version"}', tamper=True)
        check(ph.closed(), 'a tampered frame closes the connection')

        ph = Phone('127.0.0.1', port, L['id'], L['key'])
        ph.s.sendall(struct.pack('>I', 1 << 20))
        check(ph.closed(), 'an over-long frame closes the connection')

        bad = Phone('127.0.0.1', port, L['id'], os.urandom(32))
        check(not bad.confirmed, 'with the wrong key the phone sees zxvd cannot confirm')
        bad.send_frame(b'{"jsonrpc":"2.0","id":1,"method":"zxvd.version"}')
        check(bad.closed(), 'a frame under the wrong key closes the connection')
        try:
            Phone('127.0.0.1', port, os.urandom(16), L['key'])
            check(False, 'an unknown phone id gets no answer')
        except (ConnectionError, OSError):
            check(True, 'an unknown phone id gets no answer')

        r2 = rpc(sock, 'phone.pair', {'name': 'iPad', 'allow_post': True})['result']
        L2 = parse_link(r2['link'])
        p2 = Phone('127.0.0.1', port, L2['id'], L2['key'])
        r = p2.call('currency.enable', {'code': 'USD'})
        check(err_code(r) != -32012, 'a pairing with allow_post may call money methods (%s)' %
              json.dumps(r)[:120])
        check(err_code(p2.call('zxvd.shutdown')) == -32012,
              'allow_post still never allows shutdown')

        ph = Phone('127.0.0.1', port, L['id'], L['key'])
        check('result' in ph.call('swarm.state'), 'the first phone reconnects')
        rv = rpc(sock, 'phone.revoke', {'phone_id': L['id'].hex()})
        check(rv.get('result', {}).get('closed') == 1, 'phone.revoke closes its connection')
        check(ph.closed(), 'the revoked phone is disconnected')
        try:
            Phone('127.0.0.1', port, L['id'], L['key'])
            check(False, 'a revoked phone cannot reconnect')
        except (ConnectionError, OSError):
            check(True, 'a revoked phone cannot reconnect')
        check(err_code(rpc(sock, 'phone.revoke', {'phone_id': L['id'].hex()})) == -32602,
              'revoking twice is an error')
        check('result' in p2.call('ledger.check'), 'the other phone is unaffected')
        p2.close()
    finally:
        stop(proc, sock)

    proc, sock, port = start(binary, home, link_args)
    try:
        p2 = Phone('127.0.0.1', port, L2['id'], L2['key'])
        check(p2.confirmed and 'result' in p2.call('zxvd.version'),
              'pairings survive a restart')
        lst = rpc(sock, 'phone.list')['result']['phones']
        check([p['name'] for p in lst] == ['iPad'], 'the revoked pairing stays gone')
        p2.close()
    finally:
        stop(proc, sock)

    print('%d failure(s)' % len(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
