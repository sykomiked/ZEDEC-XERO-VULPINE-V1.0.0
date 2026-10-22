#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""test_zxvd.py ZXVD [GGUF] — start zxvd and drive it end to end over its
JSON-RPC socket only (zxvd_rpc.h), the way the native UI and the SDKs will.

One process, one socket, every layer: the swarm (budgets, market, settlement
spine on pay_ledger, self-audit), the model slot behind the budget gate, and
libzxv's books through its frozen ABI. Fails unless: the socket is created
0600 in a 0700 folder; discovery and versions answer; with GGUF the model
loads and an answer is charged to the companion's budget and tagged; the
swarm keeps settling with its ledger spine unhalted; the books accept a
balanced flow, refuse an overdraft, a fractional amount and a reused
reference, and verify their invariants; the protocol errors are the JSON-RPC
2.0 ones; notifications get no reply; several clients are served at once; an
over-long line closes only that client; a second zxvd cannot take the
socket; and zxvd.shutdown stops it and removes the socket file.

On Windows (zxvd.exe, zxvd_rpc.h T5) the same checks run over Winsock's
AF_UNIX, through ctypes when this Python has no socket.AF_UNIX, and the
0600/0700 checks become: the socket file's and the folder's DACLs grant
access to this user only."""
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

FAILS = []
WINDOWS = os.name == 'nt'


class _WinUnixSocket:
    """A stream AF_UNIX socket on Winsock (Windows 10 1803+), for Pythons
    whose socket module has no AF_UNIX on Windows. Only what Client uses."""

    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ct = ctypes
        self.ws = ctypes.WinDLL('ws2_32', use_last_error=True)
        self.ws.socket.restype = ctypes.c_size_t
        self.ws.socket.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        for f in ('connect', 'send', 'recv', 'closesocket', 'setsockopt'):
            getattr(self.ws, f).restype = ctypes.c_int
        self.ws.connect.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
        self.ws.send.argtypes = [ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        self.ws.recv.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.ws.closesocket.argtypes = [ctypes.c_size_t]
        self.ws.setsockopt.argtypes = [ctypes.c_size_t, ctypes.c_int, ctypes.c_int,
                                       ctypes.c_void_p, ctypes.c_int]
        socket.socket(socket.AF_INET).close()  # the socket module has run WSAStartup
        self.fd = self.ws.socket(1, 1, 0)  # AF_UNIX, SOCK_STREAM
        if self.fd == ctypes.c_size_t(-1).value:
            raise OSError('AF_UNIX socket: error %d' % ctypes.get_last_error())
        self.wintypes = wintypes

    def _err(self, what):
        raise OSError('%s: winsock error %d' % (what, self.ct.get_last_error()))

    def settimeout(self, secs):
        ms = self.wintypes.DWORD(int(secs * 1000))
        for opt in (0x1005, 0x1006):  # SO_SNDTIMEO, SO_RCVTIMEO
            self.ws.setsockopt(self.fd, 0xFFFF, opt, self.ct.byref(ms), 4)

    def connect(self, path):
        raw = path.encode()
        if len(raw) >= 108:
            raise OSError('path too long')
        addr = self.ct.create_string_buffer(2 + 108)
        addr[0:2] = (1).to_bytes(2, 'little')
        addr[2:2 + len(raw)] = raw
        if self.ws.connect(self.fd, addr, len(addr)) != 0:
            self._err('connect')

    def sendall(self, data):
        off = 0
        while off < len(data):
            k = self.ws.send(self.fd, data[off:off + 65536], min(65536, len(data) - off), 0)
            if k <= 0:
                self._err('send')
            off += k

    def recv(self, n):
        buf = self.ct.create_string_buffer(n)
        k = self.ws.recv(self.fd, buf, n, 0)
        if k < 0:
            self._err('recv')
        return buf.raw[:k]

    def close(self):
        if self.fd is not None:
            self.ws.closesocket(self.fd)
            self.fd = None


def unix_socket():
    if hasattr(socket, 'AF_UNIX'):
        return socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    return _WinUnixSocket()


def owner_only_acl(path):
    """Windows: every ACE icacls lists for `path` names this user."""
    out = subprocess.run(['icacls', path], capture_output=True, text=True, timeout=20).stdout
    me = subprocess.run(['whoami'], capture_output=True, text=True, timeout=20).stdout.strip()
    aces = [ln.replace(path, '', 1).strip() for ln in out.splitlines()
            if ':' in ln and '(' in ln and not ln.startswith('Successfully')]
    return bool(aces) and all(a.lower().startswith(me.lower() + ':') for a in aces), aces


# A Windows console's code page (cp1252) cannot print an emoji from a chat
# record; escape it rather than fail the run on a log line.
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(errors='backslashreplace')


def check(cond, msg):
    line = '  %s %s' % ('ok  ' if cond else 'FAIL', msg)
    try:
        print(line)
    except UnicodeEncodeError:  # a Windows console (cp1252) has no emoji
        print(line.encode('ascii', 'backslashreplace').decode('ascii'))
    if not cond:
        FAILS.append(msg)


class Client:
    def __init__(self, path):
        self.s = unix_socket()
        self.s.settimeout(300)  # a real model on a slow CPU can take a while
        self.s.connect(path)
        self.buf = b''
        self.next_id = 1

    def send_raw(self, line):
        self.s.sendall(line if isinstance(line, bytes) else line.encode())

    def read(self):
        while b'\n' not in self.buf:
            k = self.s.recv(65536)
            if not k:
                raise ConnectionError('closed')
            self.buf += k
        line, _, self.buf = self.buf.partition(b'\n')
        return json.loads(line)

    def call(self, method, params=None):
        rid = self.next_id
        self.next_id += 1
        req = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            req['params'] = params
        self.send_raw(json.dumps(req) + '\n')
        r = self.read()
        assert r.get('id') == rid, r
        return r

    def ok(self, method, params=None):
        r = self.call(method, params)
        if 'result' not in r:
            raise AssertionError('%s failed: %s' % (method, r))
        return r['result']

    def close(self):
        self.s.close()


SEEN = []  # every line wait_line read, for the ZXV-URL line


def wait_line(proc, prefix, secs=20):
    t0 = time.time()
    while time.time() - t0 < secs:
        line = proc.stdout.readline()
        if not line:
            return None
        SEEN.append(line.strip())
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return None


def check_modules(c, acct):
    """The module methods (zxvd_rpc_modules.h) through the real daemon: one
    round trip each, one refusal each, and the books untouched."""
    accounts = c.ok('ledger.accounts')
    journal = c.ok('ledger.entries', {'account': acct})
    # ---- freight: lose most rows, rebuild from any k ----
    data = bytes((i * 37 + 11) & 0xff for i in range(1000))
    f = c.ok('freight.pack', {'data_hex': data.hex()})
    pk = bytes.fromhex(f['packets_hex'])
    rows = [pk[i * 21:(i + 1) * 21] for i in range(168)]
    keep = rows[168 - f['k']:][::-1]  # parity rows only, in reverse
    u = c.ok('freight.unpack', {'header_hex': f['header_hex'],
                                'packets_hex': b''.join(keep).hex()})
    check(f['k'] == 50 and len(pk) == 168 * 21 and bytes.fromhex(u['data_hex']) == data,
          'freight: 1000 bytes rebuilt from 50 of 168 packets (118 lost)')
    r = c.call('freight.unpack', {'header_hex': f['header_hex'],
                                  'packets_hex': b''.join(keep[1:]).hex()})
    check(r.get('error', {}).get('code') == -32017, 'freight: k - 1 packets are refused (-32017)')
    # ---- harmonic: the wire frame ----
    h = c.ok('harmonic.encode', {'trunk': 4, 'shell': 1, 's_plus': [65536, 65536, 7],
                                 's_minus': [65536, 65536]})
    d = c.ok('harmonic.decode', {'frame_hex': h['frame_hex']})
    check(len(h['frame_hex']) == 42 and d['tag'] == 14 and d['trunk_hz'] == 555 and
          d['s_plus'] == [65536, 65536, 7] and d['truth'] == 'TRUE',
          'harmonic: a 21-octet frame round-trips and resolves TRUE')
    r = c.call('harmonic.encode', {'trunk': 10, 's_plus': [0, 0, 0], 's_minus': [0, 0]})
    check(r.get('error', {}).get('code') == -32602, 'harmonic: trunk 10 is -32602')
    # ---- stream: the cut rule and a segment's CID ----
    plan = c.ok('stream.plan', {'units': [[400, 0, True], [300, 40000, False],
                                          [500, 1000000, True]],
                                'target_us': 1000000, 'max_us': 4000000, 'tail_us': 40000})
    check([(g['first_unit'], g['len'], g['dur_us']) for g in plan['segments']] ==
          [(0, 700, 1000000), (2, 500, 40000)], 'stream.plan: cut at the next keyframe')
    seg = c.ok('stream.segment', {'data_hex': data.hex(), 'k': 20})
    cid = c.ok('web4.cid', {'data_hex': data.hex()})
    check(seg['nfreights'] == 3 and len(seg['freight_sha256']) == 3 and
          seg['cid'] == cid['cid'] and cid['uri'] == 'ipfs://' + cid['cid'],
          'stream.segment: three freights, and the CID web4.cid gives the same bytes')
    # ---- ehop: one AEAD frame on a keyed channel ----
    ch = {'key_hex': '42' * 32, 'band': 'settle', 'chan_id': 3, 'epoch': 9}
    bands = [b['name'] for b in c.ok('ehop.bands')['bands']]
    fr = c.ok('ehop.seal', dict(ch, sender=1, seq=0, plaintext_hex=data[:100].hex()))
    op = c.ok('ehop.open', dict(ch, frame_hex=fr['frame_hex']))
    bad = c.call('ehop.open', dict(ch, epoch=10, frame_hex=fr['frame_hex']))
    check(bands == ['control', 'settle', 'media', 'chat', 'bulk'] and
          bytes.fromhex(op['plaintext_hex']) == data[:100] and op['seq'] == '0' and
          bad.get('error', {}).get('code') == -32017,
          'ehop: seal and open on a settle channel; another epoch is refused')
    # ---- web4: an agent card that verifies, and one that does not ----
    card = c.ok('web4.agent_card', {'seed_hex': '5a' * 32, 'name': 'e2e-agent'})
    keys = ('bound_ms', 'pk_hex', 'bind_sig_hex')
    v = c.ok('web4.card_verify', dict({k: card[k] for k in keys}, name=card['name']))
    w = c.ok('web4.card_verify', dict({k: card[k] for k in keys}, name='e2e-other'))
    check(v['valid'] is True and v['agent_id'] == card['agent_id'] and w['valid'] is False,
          'web4: a self-bound ML-DSA-65 card verifies; renamed, it does not')
    # zxvd_rpc.h M2: none of these reached the books
    check(c.ok('ledger.accounts') == accounts and
          c.ok('ledger.entries', {'account': acct}) == journal,
          'the module methods opened no account and posted nothing')


# sdk/kits/aipi/test/aipi_mods.h, assembled: half(n) = n / 2, its inverse
# half_ok(n, r), and spin(n), a loop that only fuel stops; and "floaty",
# half(n) through an f64 that the Z-Wasm validator refuses.
HALVES = ('0061736d01000000010c0260017e017e60027e7e017f0304030001000719030468616c66000007'
          '68616c665f6f6b0001047370696e00020a2a030700200042027f0b1600200142027e2000512001'
          '42027e42017c200051720b090003400c000b42000b')
FLOATY = '0061736d0100000001060160017e017e030201000708010468616c6600000a080106002000b9b00b'
HALVES_ZC = 'fn half(n: int[0..1000000]) -> int[0..500000];\n'


def play_checks(c):
    """play.* (zxvd_rpc.h P1-P3): the Playground's templates over the socket."""
    def err(method, params):
        return c.call(method, params).get('error', {})

    cat = c.ok('play.catalogue')
    ids = [t['id'] for t in cat]
    check(len(cat) >= 10 and 'payment-reconciliation' in ids and 'trade-corridor' in ids,
          'play.catalogue lists the templates (%d)' % len(cat))
    check(all(t['files'] and t['steps'] for t in cat), 'play.catalogue: every template has files and steps')
    r = c.ok('play.run', {'template': 'payment-reconciliation'})
    check(r['verdict'] == 'BOTH' and r['approval'] == 'required' and r['steps'][1]['ran'] is False,
          'play.run: a conflicting report is held (BOTH) and the gated settlement does not run')
    inputs = ('payments = P-1001 P-1002 P-1003 P-1004\nsender = 1 1 1 1\n'
              'receiver = 1 1 1 1\nrtgs = 1 1 1 1\n')
    r = c.ok('play.run', {'template': 'payment-reconciliation', 'files': {'inputs.txt': inputs}})
    outs = {o['name']: o['value'] for o in r['steps'][1]['outputs']}
    check(r['verdict'] == 'TRUE' and outs.get('fees') == '755',
          'play.run: with the edit every payment is clear and the treaty settles (fee 755)')
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
    bad = open(os.path.join(root, 'sdk', 'playground', 'templates', 'trade-corridor',
                            'corridor.treaty')).read().replace('3000000 3000 1/7', '3000000 3000001 1/7')
    r = c.ok('play.run', {'template': 'trade-corridor', 'files': {'corridor.treaty': bad}})
    e = r['steps'][0].get('error', {})
    check(r['verdict'] == 'FALSE' and e.get('name') == 'ZXT_E_CLAIM' and e.get('line') == 5,
          'play.run: a claim above its deposit is refused at line 5')
    check(err('play.run', {'template': 'no-such'}).get('code') == -32016, 'play.run: unknown template')
    r = c.call('play.run', {'template': 'trade-corridor', 'files': {'../x': 'y'}})
    check(r.get('result', {}).get('verdict') == 'FALSE' or 'error' in r,
          'play.run: a file name with a directory is refused')
    check(err('play.run', {'template': 'trade-corridor', 'files': ['x']}).get('code') == -32602,
          'play.run: files must be an object')
    d = c.ok('play.draft', {'template': 'trade-corridor', 'request': 'Acme owes Beta 12000 dollars'})
    check(isinstance(d['draft'], str) and isinstance(d['model'], bool),
          'play.draft returns the swarm\'s draft as untrusted text (model=%s)' % d['model'])
    check(err('play.draft', {'template': 'relief-allocation'}).get('code') == -32016,
          'play.draft: a template without a draft line')
    play_http_checks()


def play_http_checks():
    """The window's Playground routes (zxv_host.c): the same results as JSON
    the page can parse, with this launch's token."""
    url = [l for l in SEEN if l.startswith('ZXV-URL: http://127.0.0.1:')]
    check(len(url) == 1, 'zxvd prints its window URL')
    if not url:
        return
    port = int(url[0].split(':')[3].split('/')[0])
    token = url[0].split('#token=')[1]

    def http(method, path, body=b''):
        s = socket.create_connection(('127.0.0.1', port), timeout=30)
        head = ('%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nSec-Fetch-Site: same-origin\r\n'
                'X-ZXV-Token: %s\r\nContent-Length: %d\r\n\r\n' % (method, path, port, token, len(body)))
        s.sendall(head.encode() + body)
        data = b''
        while True:
            k = s.recv(65536)
            if not k:
                break
            data += k
        s.close()
        hdr, _, rest = data.partition(b'\r\n\r\n')
        return int(hdr.split(b' ', 2)[1]) if hdr.startswith(b'HTTP/1.1 ') else 0, rest

    st, body = http('GET', '/api/play/catalogue')
    check(st == 200 and len(json.loads(body)) >= 10, 'GET /api/play/catalogue (%d)' % st)
    st, body = http('POST', '/api/play/run', b'template payment-reconciliation\n')
    check(st == 200 and json.loads(body)['verdict'] == 'BOTH', 'POST /api/play/run (%d)' % st)
    req = b'Acme owes "Beta" 12000\\n dollars'
    st, body = http('POST', '/api/play/draft',
                    b'template trade-corridor\nrequest %d\n%s\n' % (len(req), req))
    try:
        d = json.loads(body)
    except ValueError:
        d = {}
    check(st == 200 and isinstance(d.get('draft'), str) and isinstance(d.get('model'), bool),
          'POST /api/play/draft answers JSON the page can parse (%d)' % st)
    st, _ = http('POST', '/api/play/draft', b'template relief-allocation\n')
    check(st == 404, 'POST /api/play/draft: a template without a draft line (%d)' % st)


def aipi_checks(c, home):
    def err(method, params):
        r = c.call(method, params)
        return r.get('error', {})

    st0 = c.ok('aipi.status')
    check(st0['online'] is False and st0['blocks'] == 0 and st0['store_cap'] == 32 << 20,
          'aipi.status: offline node, empty in-memory blockstore')
    mod = c.ok('aipi.put', {'hex': HALVES})
    check(mod['bytes'] == len(HALVES) // 2 and mod['cid'].startswith('z') and
          mod['private'] is False, 'aipi.put stores the Wasm module by CID (%s)' % mod['cid'])
    path = os.path.join(home, 'halves.zc')
    with open(path, 'w') as f:
        f.write(HALVES_ZC)
    con = c.ok('aipi.put', {'path': path})
    check(con['bytes'] == len(HALVES_ZC), 'aipi.put reads the contract from a file')
    g = c.ok('aipi.get', {'cid': mod['cid']})
    check(g['hex'] == HALVES and g['tag']['base'] == 'true',
          'aipi.get returns the verified bytes and a TRUE tag')
    check(err('aipi.put', {'hex': 'abc'}).get('code') == -32602 and
          err('aipi.put', {'hex': HALVES, 'path': path}).get('code') == -32602 and
          err('aipi.put', {'path': os.path.join(home, 'missing')}).get('code') == -32011,
          'aipi.put refuses odd hex, both sources at once and a missing file')
    fn = c.ok('aipi.publish', {'module': mod['cid'], 'contract': con['cid'], 'entry': 'half',
                               'checker': 'half_ok', 'verify': 'checker', 'fuel_max': 100000})
    check(fn['function'].startswith('z') and fn['local_only'] is False,
          'aipi.publish returns the function CID')
    x = c.ok('aipi.execute', {'function': fn['function'], 'args': [2026]})
    check(x['result'] == '1013' and x['route'] == 'local' and x['tag']['phase'] == 'manifest',
          'aipi.execute: half(2026) = 1013, run here and verified (%s)' % x['tag'])
    y = c.ok('aipi.execute', {'function': fn['function'], 'args': [2026]})
    check(y['result'] == '1013' and y['route'] == 'cache' and y['call'] == x['call'],
          'the same call is answered from the memo of verified results')
    e = err('aipi.execute', {'function': fn['function'], 'args': [2000001]})
    check(e.get('code') == -32011, 'the contract refuses an argument outside its range (%s)' % e)
    e = err('aipi.execute', {'function': fn['function'], 'args': [1.5]})
    check(e.get('code') == -32602, 'a fractional argument is refused, never rounded')
    spin = c.ok('aipi.publish', {'module': mod['cid'], 'entry': 'spin', 'verify': 'local',
                                 'fuel_max': 5000, 'local_only': True})
    e = err('aipi.execute', {'function': spin['function'], 'args': [1]})
    check(e.get('code') == -32011 and spin['local_only'] is True,
          'a loop runs out of fuel and fails, the swarm loop keeps going (%s)' % e.get('message'))
    fl = c.ok('aipi.put', {'hex': FLOATY})
    ff = c.ok('aipi.publish', {'module': fl['cid'], 'entry': 'half', 'verify': 'local',
                               'fuel_max': 1000})
    e = err('aipi.execute', {'function': ff['function'], 'args': [4]})
    check(e.get('code') == -32011, 'a module with floats is refused by the validator')
    check(err('aipi.publish', {'module': mod['cid'], 'entry': 'half', 'verify': 'checker',
                               'fuel_max': 10}).get('code') == -32011 and
          err('aipi.get', {'cid': 'not-a-cid'}).get('code') == -32011,
          'a checker mode without a checker, and a bad CID, are refused')
    st = c.ok('aipi.status')
    check(st['calls'] == 5 and st['failed'] == 3 and st['blocks'] >= 5 and
          st['sandbox']['trapped'] >= 1 and st['sandbox']['refused_module'] >= 1 and
          int(st['sandbox']['fuel_used']) > 0,
          'aipi.status counts the calls, failures and sandbox refusals (%s)' % st)


def payment_checks(c, usd_account):
    """zxvd_pay.h P1-P5 over the socket, on the in-memory books."""
    def err(method, params):
        return c.call(method, params).get('error', {})

    # P6: netting, escrow and passkeys start off; card.check and ledger.holds need no switch
    check(all(err(m, {}).get('code') == -32012 for m in
              ('netting.list', 'ledger.hold', 'ledger.release', 'ledger.refund', 'passkey.verify')) and
          'holds' in c.ok('ledger.holds'),
          'payments: netting, escrow and passkeys are off until switched on')
    for f in ('netting', 'escrow', 'passkeys'):
        c.ok('features.set', {'feature': f})
    check(c.ok('card.check', {'network': 'dragon', 'pan': '79927398713'})['valid'] is True and
          c.ok('card.check', {'network': 'dragon', 'pan': '79927398710'})['valid'] is False,
          'card.check: a Luhn check digit')
    e = err('card.check', {'network': 'visa', 'pan': '1'})
    check(e.get('code') == -32602 and 'phoenix' in e.get('message', ''),
          'card.check: only the platform\'s networks (%s)' % e)
    e = err('card.check', {'network': 'dragon', 'pan': '12a4'})
    check(e.get('data', {}).get('name') == 'ZXV_E_ARG', 'card.check: digits only (%s)' % e)

    fields = {'msg_id': 'E2E-MSG-1', 'cre_dt_tm': '2026-10-09T12:00:00Z',
              'debtor_name': 'Ada & Sons', 'debtor_acct': 'NG0001', 'creditor_name': 'Kwame',
              'creditor_acct': 'GH0002', 'end_to_end_id': 'E2E-1', 'ccy': 'NGN'}
    m = c.ok('iso20022.render', {'kind': 'pacs008', 'fields': fields,
                                 'amounts': {'settlement': 250050}})
    check(m['kind'] == 'pacs008' and 'pacs.008.001.09' in m['xml'] and '2500.50' in m['xml'] and
          'Ada &amp; Sons' in m['xml'], 'iso20022.render: a pacs.008')
    e = err('iso20022.render', {'kind': 'pacs008', 'fields': dict(fields, ccy='VFV'),
                                'amounts': {'settlement': 1}})
    check(e.get('data', {}).get('name') == 'ZXV_E_CURRENCY',
          'iso20022.render: VFV never goes on the wire (%s)' % e)
    e = err('iso20022.render', {'kind': 'pacs008', 'fields': {'iban': 'X'}})
    check(e.get('code') == -32602 and e.get('message', '').startswith('unknown field "iban"'),
          'iso20022.render: an unknown field (%s)' % e)

    c.ok('netting.open', {'cycle': 'E2E-NET', 'currency': 'KES'})
    for who in ('A', 'B', 'C'):
        c.ok('netting.add', {'cycle': 'E2E-NET', 'participant': who})
    for pid, a, b, amt in (('p1', 'A', 'B', 100), ('p2', 'B', 'C', 70), ('p3', 'C', 'A', '20')):
        c.ok('netting.submit', {'cycle': 'E2E-NET', 'payment_id': pid, 'debtor': a, 'creditor': b,
                                'amount_minor': amt})
    again = c.ok('netting.submit', {'cycle': 'E2E-NET', 'payment_id': 'p1', 'debtor': 'A',
                                     'creditor': 'B', 'amount_minor': 100})
    e = err('netting.submit', {'cycle': 'E2E-NET', 'payment_id': 'p1', 'debtor': 'A',
                               'creditor': 'B', 'amount_minor': 99})
    check(again['accepted'] is True and e.get('data', {}).get('name') == 'ZXV_E_DUPLICATE',
          'netting.submit: idempotent on payment_id, another amount is refused (%s)' % e)
    e = err('netting.positions', {'cycle': 'E2E-NET'})
    check(e.get('code') == -32020, 'netting.positions: not before the close (%s)' % e)
    cy = c.ok('netting.close', {'cycle': 'E2E-NET'})
    check(cy['closed'] is True and cy['sum_minor'] == '0' and
          [(p['participant'], p['net_minor']) for p in cy['positions']] ==
          [('A', '-80'), ('B', '30'), ('C', '50')], 'netting.close: positions net to zero (%s)' % cy)
    check(c.ok('netting.drop', {'cycle': 'E2E-NET'}) == {'dropped': 'E2E-NET'} and
          c.ok('netting.list') == {'cycles': []}, 'netting.drop')

    e = err('passkey.register', {'rp_id': 'zxv.id', 'authenticator_data': '00' * 2033})
    check(e.get('code') == -32020, 'passkey.register: not a passkey registration (%s)' % e)
    e = err('passkey.verify', {'rp_id': 'zxv.id', 'credential_id': '00' * 16,
                               'authenticator_data': '00' * 37, 'client_data_hash': '00' * 32,
                               'signature': '00' * 3309})
    check(e == {'code': -32020, 'message': 'no passkey with that "credential_id" is held'},
          'passkey.verify: an unknown credential (%s)' % e)

    c.ok('currency.enable', {'code': 'KES'})
    iss = c.ok('ledger.open_account', {'name': 'kes bank', 'currency': 'KES',
                                       'kind': 'issuer'})['account']
    buyer = c.ok('ledger.open_account', {'name': 'buyer', 'currency': 'KES'})['account']
    seller = c.ok('ledger.open_account', {'name': 'seller', 'currency': 'KES'})['account']
    c.ok('ledger.post', {'from': iss, 'to': buyer, 'amount_minor': 1000, 'currency': 'KES',
                         'reference': 'E2E-KES-1'})
    h1 = c.ok('ledger.hold', {'from': buyer, 'to': seller, 'amount_minor': 600})
    check(h1['state'] == 'held' and h1['amount_minor'] == '600' and h1['currency'] == 'KES' and
          h1['closed_entry_id'] is None and h1['releases_on'] == 'owner',
          'ledger.hold: held (%s)' % h1)
    esc = h1['escrow_account']
    check(c.ok('ledger.balance', {'account': buyer})['equity_minor'] == '400' and
          c.ok('ledger.balance', {'account': esc})['equity_minor'] == '600',
          'ledger.hold: the funds sit in the escrow account')
    e = err('ledger.hold', {'from': buyer, 'to': seller, 'amount_minor': 401})
    check(e.get('data', {}).get('name') == 'ZXV_E_FUNDS', 'ledger.hold: no overdraft (%s)' % e)
    e = err('ledger.hold', {'from': buyer, 'to': usd_account, 'amount_minor': 1})
    check(e.get('code') == -32602 and 'a hold is in one currency' in e.get('message', ''),
          'ledger.hold: one currency (%s)' % e)
    e = err('ledger.hold', {'from': esc, 'to': seller, 'amount_minor': 1})
    check(e.get('code') == -32602 and 'moved only by ledger.release' in e.get('message', ''),
          'ledger.hold: not out of an escrow account (%s)' % e)
    check('result' in c.call('ledger.holds'), 'ledger.holds answers')
    r = c.ok('ledger.release', {'hold': h1['hold']})
    check(r['state'] == 'released' and r['closed_entry_id'] is not None and
          c.ok('ledger.balance', {'account': seller})['equity_minor'] == '600',
          'ledger.release: the payee is paid (%s)' % r)
    e = err('ledger.release', {'hold': h1['hold']})
    check(e == {'code': -32020, 'message': 'hold %d is already released' % h1['hold']},
          'ledger.release: once (%s)' % e)
    h2 = c.ok('ledger.hold', {'from': buyer, 'to': seller, 'amount_minor': '150'})
    check(h2['hold'] == h1['hold'] + 1, 'hold numbers count up')
    r = c.ok('ledger.refund', {'hold': h2['hold']})
    check(r['state'] == 'refunded' and
          c.ok('ledger.balance', {'account': buyer})['equity_minor'] == '400',
          'ledger.refund: the payer gets it back (%s)' % r)
    e = err('ledger.refund', {'hold': 999})
    check(e == {'code': -32020, 'message': 'no hold 999'}, 'ledger.refund: no such hold (%s)' % e)
    hs = c.ok('ledger.holds')
    check([(h['hold'], h['state']) for h in hs['holds']] ==
          [(h1['hold'], 'released'), (h2['hold'], 'refunded')] and
          hs['escrow_accounts'][0]['consistent'] is True and
          hs['escrow_accounts'][0]['held_minor'] == '0' and hs['unmatched'] == 0,
          'ledger.holds (%s)' % hs)
    check(c.ok('ledger.check')['ok'] is True, 'the books balance after escrow')
def econ_checks(c):
    """bonds.*, derivatives.*, grants.*, estate.* over the socket (zxvd_sandbox.h)."""
    fs = {f['name']: f for f in c.ok('features.get')['features']}
    if 'estate' not in fs:
        check(True, 'economy groups: not in this build')
        return
    check(all(fs[n]['regulated'] for n in ('bonds', 'derivatives', 'grants')) and
          not fs['estate']['regulated'] and not any(fs[n]['enabled'] for n in
                                                     ('bonds', 'derivatives', 'grants', 'estate')),
          'economy groups: bonds, derivatives, grants regulated; all four start off')
    r = c.call('bonds.setup', {'juris': 1, 'gateway': 900})
    check(r.get('error', {}).get('code') == -32012 and 'regulated' in r['error']['message'],
          'bonds.setup while off answers -32012 (%s)' % r.get('error'))
    st = c.ok('derivatives.status')
    check(st['leverage'] is False and st['set_up'] is False,
          'derivatives.status answers while off, never with leverage')
    c.ok('features.set', {'feature': 'estate'})
    fa = c.ok('estate.faraid', {'husband': True, 'daughters': 1, 'amount_minor': 1200})
    check(fa['ruling'] is False and fa['advisory'] is True and 'not a ruling' in fa['notice'],
          'estate.faraid computes shares as advice, not a ruling')
    r = c.call('estate.begin')
    check(r.get('error', {}).get('code') == -32060, 'estate.begin before setup answers -32060')
    c.ok('features.set', {'feature': 'estate', 'enabled': False})


NAMED = {}  # the chiglet name_checks gave, checked again after a restart


def name_checks(c, env):
    """companion.name, companion.name_preview, companion.name_confirm (zxvd_companion.h
    H7-H11): named once, kept in the state folder, the personality grown from the name."""
    check(c.ok('companion.name') == {'named': False, 'name': None}, 'companion.name: not named yet')
    r = c.call('companion.name_preview', {'name': 'Mega Man'})
    check(r.get('error', {}).get('code') == -32072 and
          r['error']['message'].startswith('CHGN_E_RESERVED:'),
          'companion.name_preview refuses a reserved name (%s)' % r)
    p = c.ok('companion.name_preview', {'name': 'Lumi'})
    check(p['name'] == 'Lumi' and p['seed'].isdigit() and len(p['confirm']) == 64 and
          len(p['genes']) == 8 and 'home' in p['traits'],
          'companion.name_preview shows the personality and a confirm digest (%s)' % p)
    check(c.ok('companion.name')['named'] is False, 'a preview keeps nothing')
    other = c.ok('companion.name_preview', {'name': 'Pip'})
    r = c.call('companion.name_confirm', {'name': 'Lumi', 'confirm': other['confirm']})
    check(r.get('error', {}).get('code') == -32072 and
          r['error']['message'].startswith('CHGN_E_DIGEST:'),
          'companion.name_confirm needs the digest of the name shown (%s)' % r)
    n = c.ok('companion.name_confirm', {'name': 'Lumi', 'confirm': p['confirm']})
    check(n == {'named': True, 'name': 'Lumi'}, 'companion.name_confirm names it (%s)' % n)
    d = c.ok('companion.dna')
    check(d['seed'] == p['seed'] and d['genes'] == p['genes'] and d['agent_id'] == 1,
          'the companion has the name\'s personality (%s)' % d)
    for name in ('Lumi', 'Pip'):
        r = c.call('companion.name_preview', {'name': name})
        check(r.get('error', {}).get('code') == -32072 and
              r['error']['message'].startswith('CHGN_E_LOCKED:'),
              'once named, no previews (%s)' % r)
    r = c.call('companion.name_confirm', {'name': 'Pip', 'confirm': other['confirm']})
    check(r.get('error', {}).get('code') == -32072 and
          r['error']['message'].startswith('CHGN_E_LOCKED:'),
          'the name can never change (%s)' % r)
    # the host's state folder (zxv_host.c pal_state_dir): XDG on Linux, the app folder on macOS
    # and Windows
    if sys.platform == 'darwin':
        rec = os.path.join(env['HOME'], 'Library', 'Application Support', 'ZXV', 'net', 'chiglet_name')
    elif WINDOWS:
        rec = os.path.join(env['APPDATA'], 'ZXV', 'net', 'chiglet_name')
    else:
        rec = os.path.join(env['XDG_DATA_HOME'], 'zxv', 'net', 'chiglet_name')
    check(os.path.isfile(rec) and os.path.getsize(rec) == 79 and open(rec, 'rb').read(4) == b'ZXCN',
          'the name is kept in the state folder (%s)' % rec)
    if not WINDOWS:
        check(stat.S_IMODE(os.stat(rec).st_mode) == 0o600, 'the name record is 0600')
    NAMED.update(name='Lumi', seed=p['seed'], genes=p['genes'])


def name_after_restart(c):
    """The same chiglet after a restart: its name and its personality."""
    check(c.ok('companion.name') == {'named': True, 'name': NAMED.get('name')},
          'the chiglet answers to its name after a restart')
    d = c.ok('companion.dna')
    check(d['seed'] == NAMED.get('seed') and d['genes'] == NAMED.get('genes'),
          'and has the same personality (%s)' % d)
# A TEST-ONLY Ed25519 gateway key (seed 32 x 0x42) and its signature over the
# V-Bonds switch manifest for juris 1, epoch 1, enable (vbond.h B0), made
# once with the cryptography package so this test needs no signer.
CARD_GW_PK = '2152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12'
CARD_SW_MSG = '7a78762f76626f6e642d73772f763101000000010100000000000000'
CARD_SW_SIG = ('a9dbfeb6cbaae1d2de149cb3152ea8c02696407f2995b5175fbda3598dfb6b8c'
               '7733d2aa887787af9a858d38d61075b519c1300355065a72ede1e1e8d8191205')


def card_practice_checks(c, pc):
    """player.card (zxvd_rpc.h V3): practice units on the V-Bonds desk are
    shown under "practice" and never move the tier or fill an input."""
    pb = pc.get('practice', {}).get('bonds', {})
    check(pb.get('set_up') is False and pb.get('value') == '0' and pb.get('live') == 0 and
          'never a tier input' in pb.get('source', '') and
          'practice units; not counted' in pc['inputs']['bond']['source'] and
          'practice units; not counted' in pc['inputs']['giving']['source'] and
          pc['inputs']['giving']['value'] == '0',
          'player.card: bond and giving name why they are 0; no practice desk yet (%s)' % pc)
    fs = {f['name']: f for f in c.ok('features.get')['features']}
    if 'bonds' not in fs:
        check(True, 'player.card practice bonds: not in this build')
        return
    c.ok('features.set', {'feature': 'bonds'})
    c.ok('bonds.setup', {'juris': 1, 'gateway': 900})
    c.ok('bonds.key', {'key': 900, 'public_key': CARD_GW_PK})
    vb = c.ok('bonds.account_open', {'owner': 1, 'asset': 'vbill'})['account']
    pay = c.ok('bonds.account_open', {'owner': 1, 'asset': 'vino'})['account']
    # beyond Platinum's bond threshold (sicily.h S2: 1,000,000)
    c.ok('bonds.fund', {'account': vb, 'amount_minor': '2000000'})
    m = c.ok('bonds.message', {'kind': 'switch', 'epoch': 1})
    check(m['message'] == CARD_SW_MSG, 'bonds.message: the switch manifest this test signed')
    c.ok('bonds.switch', {'epoch': 1, 'sig': CARD_SW_SIG})
    b = c.ok('bonds.issue', {'owner': 1, 'vbill_account': vb, 'payee': pay, 'qty': '1500000',
                             'k': 13})
    check(b['qty'] == '1500000', 'a practice V-Bond of 1,500,000 is live (%s)' % b)
    p2 = c.ok('player.card')
    check(p2['practice']['bonds'] == dict(pb, set_up=True, value='1500000', live=1),
          'player.card: the practice bond is shown apart (%s)' % p2['practice'])
    check(p2['tier'] == pc['tier'] == 0 and p2['metal'] == 'Copper' and
          p2['inputs'] == pc['inputs'] and p2['next'] == pc['next'] and p2['perks'] == [],
          'player.card: practice units move no tier, input or missing list (%s)' % p2)
    # the switch off keeps the desk; the card still reads it, still uncounted
    c.ok('features.set', {'feature': 'bonds', 'enabled': False})
    p3 = c.ok('player.card')
    check(p3['practice']['bonds']['value'] == '1500000' and p3['tier'] == 0 and
          p3['inputs']['bond']['value'] == '0', 'player.card with bonds off: the same (%s)' % p3)


def companion_checks(c):
    """companion.dna, companion.generation, companion.mood (zxvd_companion.h H1-H6)."""
    fs = {f['name']: f for f in c.ok('features.get')['features']}
    check('companion' in fs and not fs['companion']['regulated'] and
          not fs['companion']['enabled'], 'companion: a feature that is not regulated, off')
    genes = ['emotion', 'temperament', 'curiosity', 'caution', 'warmth', 'pace', 'verbosity',
             'specialty']
    d0 = c.ok('companion.dna')
    t = d0['traits']
    check(d0['agent_id'] == 1 and list(d0['genes']) == genes and
          all(0 <= v <= 255 for v in d0['genes'].values()) and d0['seed'].isdigit() and
          d0['founder'] == (d0['parent_a'] == '0' and d0['parent_b'] == '0') and
          t['home']['emotion'] == ['neutral', 'calm', 'joy', 'curious', 'love', 'worry',
                                   'frustration', 'sadness'][d0['genes']['emotion'] % 8] and
          t['home']['intensity'] == d0['genes']['temperament'] % 6 and
          isinstance(t['specialty_name'], str),
          'companion.dna answers with the feature off: 8 genes, expressed traits (%s)' % d0)
    m = c.ok('companion.mood')
    check(m['home'] == t['home'] and m['feeling']['intensity'] <= 5 and
          m['feeling']['charge'] in (0, 1, 2, 3, 5, 8) and len(m['feeling']['emoji']) == 1 and
          ord(m['feeling']['emoji']) == m['feeling']['codepoint'] and
          m['at_home'] == (m['feeling']['emotion'] == m['home']['emotion']) and
          'emotion' in m['swarm_mood'],
          'companion.mood: the emoji feeling, its charge, home from DNA (%s)' % m)
    # swarm_evolve.h V7: on a GROWTH harmonic (~11 s apart) the companion steps
    # when another agent is strictly fitter; with the stand-in market all
    # fitnesses can tie, so a run may see no step (the unit test covers V7)
    g0 = c.ok('companion.generation')
    check(g0['generation'] == d0['generation'] and g0['seed'] == d0['seed'],
          'companion.generation agrees with companion.dna')
    t0 = time.time()
    g1 = g0
    while (g1['steps'] == g0['steps'] and g1['growths'] < g0['growths'] + 2 and
           time.time() - t0 < 45):
        time.sleep(0.25)
        g1 = c.ok('companion.generation')
    d1 = c.ok('companion.dna')
    agents = c.ok('swarm.state')['agents']
    check(g1['growths'] > g0['growths'] and d1['generation'] == agents[0]['gen'] and
          g1['generation'] == d1['generation'] and g1['growths'] >= g1['last_step'],
          'companion.generation follows the GROWTH harmonic (%s -> %s)' % (g0, g1))
    if g1['steps'] > g0['steps']:
        check(g1['generation'] > g0['generation'] and g1['last_step'] > g0['growths'] and
              g1['seed'] != g0['seed'] and not d1['founder'] and
              (g1['steps'] != g0['steps'] + 1 or d1['parent_a'] == g0['seed']),
              'the companion evolved: generation %d -> %d, parent %s' %
              (g0['generation'], g1['generation'], d1['parent_a']))
    else:
        check(g1['generation'] == g0['generation'] and g1['seed'] == g0['seed'] and
              g1['last_step'] == g0['last_step'],
              'no agent was fitter than the companion: its DNA and generation stay (%s)' % g1)


def web4_checks(c):
    """web4.* over the socket: the owner's Web4 envelope feed (zxvd_web4.c)."""
    fs = {f['name']: f for f in c.ok('features.get')['features']}
    if 'web4' not in fs:
        check(True, 'web4: not in this build')
        return
    check(not fs['web4']['regulated'] and not fs['web4']['enabled'],
          'web4: not regulated, starts off')
    f = c.ok('web4.feed')
    check(f['open'] is False and f['envelopes'] == [], 'web4.feed answers while off: not open')
    r = c.call('web4.open')
    check(r.get('error', {}).get('code') == -32012, 'web4.open while off answers -32012')
    c.ok('features.set', {'feature': 'web4'})
    me = c.ok('web4.open', {'name': 'chiglet'})
    check(me['new'] and len(me['agent_id']) == 64 and len(me['pk']) == 2 * 1952,
          'web4.open: an ML-DSA-65 companion key')
    check(c.ok('web4.open')['new'] is False, 'web4.open again: the same feed')
    to = 'ab' * 32
    e1 = c.ok('web4.post', {'kind': 'REQUEST', 'to': to, 'payload': 'hello, fox'})
    check(e1['status'] == 'signed' and e1['seq'] == '1' and e1['req_id'] == str(e1['id']),
          'web4.post: a plain REQUEST is signed at once')
    env = c.ok('web4.envelope', {'id': e1['id']})
    check(env['bytes'] == e1['bytes'] and len(env['envelope']) == 2 * env['bytes'],
          'web4.envelope: the signed bytes')
    v = c.ok('web4.verify', {'envelope': env['envelope'], 'pk': me['pk']})
    check(v['valid'] and v['kind'] == 'REQUEST' and v['from'] == me['agent_id'] and v['to'] == to,
          'web4.verify: the envelope checks against the companion key')
    e2 = c.ok('web4.post', {'kind': 'REQUEST', 'to': to, 'tool': 'share', 'payload': 'my email',
                            'pii': True})
    check(e2['status'] == 'held' and e2['seq'] is None and e2['bytes'] == 0,
          'web4.post: personal data is held, unsigned')
    r = c.call('web4.envelope', {'id': e2['id']})
    check(r.get('error', {}).get('code') == -32090, 'a held envelope never leaves (-32090)')
    g = c.ok('web4.consent', {'id': e2['id'], 'grant': True})
    check(g['envelope']['status'] == 'signed' and g['consent']['kind'] == 'CONSENT' and
          g['consent']['answers'] == e2['id'] and g['envelope']['consent'] == g['consent']['id'],
          'web4.consent grant: a signed CONSENT answer, then the envelope is sealed')
    v = c.ok('web4.verify', {'id': e2['id']})
    check(v['valid'] and v['consent'] and v['consent_scope'] == {'pii': True, 'money': False},
          'web4.verify: the consent token is attached and checks')
    e3 = c.ok('web4.post', {'kind': 'REQUEST', 'to': to, 'tool': 'pay', 'payload': 'invoice',
                            'amount_minor': '1500'})
    check(e3['status'] == 'held' and e3['amount_minor'] == '1500', 'web4.post: money is held')
    d = c.ok('web4.consent', {'id': e3['id'], 'grant': False})
    check(d['envelope']['status'] == 'denied' and d['envelope']['payload'] == '' and
          d['consent']['grant'] is False, 'web4.consent deny: dropped, never signed')
    f = c.ok('web4.feed', {'limit': 2})
    check([x['id'] for x in f['envelopes']] == [d['consent']['id'], e3['id']],
          'web4.feed: newest first')
    check(f['next'] == e3['id'] and f['total'] == 5 and f['held'] == 0,
          'web4.feed: paged by next')
    c.ok('features.set', {'feature': 'web4', 'enabled': False})


def seats_checks(c):
    """zxvd_seats.c L1-L7 over the socket, in memory (no --books here)."""
    fs = {f['name']: f for f in c.ok('features.get')['features']}
    check(fs['seats']['enabled'] is False and fs['seats']['regulated'] is False,
          'seats: a feature that is not regulated')
    check(c.ok('seats.list') == {'seats': [], 'auto_enter': True} and
          c.ok('seats.auto') == {'seat': None}, 'seats.list answers with its switch off: no seats')
    o = c.ok('seats.add', {'name': 'Owner \u00e9', 'role': 'person', 'lock': 'pass',
                           'pass': 'correct horse'})
    check(o['role'] == 'owner' and o['lock'] == 'pass' and len(o['id']) == 32,
          'seats.add: the first seat needs no session and becomes the owner (%s)' % o)
    e = c.call('seats.add', {'name': 'Kid', 'session': '00' * 32})
    check(e.get('error', {}).get('code') == -32080 and
          e['error']['message'].startswith('SEAT_ERR_DENIED'),
          'seats.add: an unknown session is refused (%s)' % e.get('error'))
    w = c.ok('seats.unlock', {'id': o['id'], 'pass': 'wrong horse'})
    check(w == {'ok': False, 'status': 'SEAT_ERR_WRONG', 'wait_ms': 0},
          'seats.unlock: a wrong passphrase is a result (%s)' % w)
    u = c.ok('seats.unlock', {'id': o['id'], 'pass': 'correct horse'})
    check(u['ok'] is True and len(u['session']) == 64 and u['id'] == o['id'],
          'seats.unlock: Argon2id at 64 MiB opens the seat with a session')
    k = c.ok('seats.add', {'name': 'Kid', 'session': u['session'], 'color': 255})
    ku = c.ok('seats.unlock', {'id': k['id']})
    e = c.call('seats.remove', {'id': o['id'], 'session': ku['session']})
    check(e.get('error', {}).get('message', '').startswith('SEAT_ERR_DENIED'),
          'seats.remove: a person may not remove (%s)' % e.get('error'))
    e = c.call('seats.remove', {'id': o['id'], 'session': u['session']})
    check(e.get('error', {}).get('message', '').startswith('SEAT_ERR_LAST_OWNER'),
          'seats.remove: the last owner stays')
    lst = c.ok('seats.list')
    check([(s['name'], s['role'], s['color']) for s in lst['seats']] ==
          [('Owner \u00e9', 'owner', 0), ('Kid', 'person', 255)] and
          not any(k_ in json.dumps(lst) for k_ in ('hash', 'salt', 'kdf')),
          'seats.list: both seats, no hash or salt (%s)' % lst)
    check(c.ok('seats.remove', {'id': k['id'], 'session': u['session']}) ==
          {'removed': k['id']}, 'seats.remove: an owner removes a person')
    check(c.ok('seats.lock', {'session': ku['session']}) == {'ended': False},
          'removing a seat ended its session')
    check(c.ok('seats.set_lock', {'id': o['id'], 'lock': 'none', 'old_pass': 'correct horse',
                                  'session': u['session']})['lock'] == 'none' and
          c.ok('seats.auto') == {'seat': o['id']},
          'seats.auto: one seat with no lock enters directly')
    check(c.ok('seats.lock', {'session': u['session']}) == {'ended': True}, 'seats.lock')


def space_checks(c):
    """pspace.* and hall.* (zxvd_pspace.h W1-W6) on one node."""
    def err(method, params):
        r = c.call(method, params)
        return r.get('error', {})

    fs = {f['name']: f for f in c.ok('features.get')['features']}
    check(fs['pspace']['enabled'] is False and fs['pspace']['regulated'] is False,
          'pspace: a feature that is not regulated, off (%s)' % fs.get('pspace'))
    e = err('pspace.create', {'kind': 'hall', 'gov': 'owner', 'name': 'Movie night'})
    check(e.get('code') == -32012, 'pspace.create: off until switched on (%s)' % e)
    check(c.ok('pspace.list') == {'online': False, 'spaces': []}, 'pspace.list answers while off')
    c.ok('features.set', {'feature': 'pspace'})

    check(c.ok('pspace.list') == {'online': False, 'spaces': []}, 'pspace.list: none while off')
    e = err('pspace.create', {'kind': 'hall', 'gov': 'owner', 'name': 'Movie night'})
    check(e.get('code') == -32032 and 'off' in e.get('message', ''),
          'pspace.create while off: the network is off (%s)' % e)
    e = err('pspace.create', {'kind': 'tent', 'gov': 'owner', 'name': 'x'})
    check(e.get('code') == -32602, 'pspace.create: an unknown kind is refused')
    check(c.ok('net.command', {'command': 'lan'})['result'] == 'networking is on', 'network on')
    try:
        made = c.ok('pspace.create', {'kind': 'hall', 'gov': 'owner', 'name': 'Movie night',
                                     'topic': 'film'})
        cid = made['cid']
        check(cid.startswith('b') and len(cid) == 59, 'pspace.create: a CIDv1 (%s)' % cid)
        sl = c.ok('pspace.list')['spaces']
        check(len(sl) == 1 and sl[0]['cid'] == cid and sl[0]['phase'] == 'in' and
              sl[0]['kind'] == 'hall' and sl[0]['name'] == 'Movie night' and
              sl[0]['hall']['may_control'] is True and sl[0]['members'] == 1,
              'pspace.list: in the hall, its owner (%s)' % sl)
        e = err('hall.play', {'cid': cid})
        check(e.get('code') == -32032 and 'hall.load' in e.get('message', ''),
              'hall.play with nothing loaded is refused')
        h = c.ok('hall.load', {'cid': cid, 'media': 'bafkreigh2akiscaildc'})
        check(h['state'] == 'stop' and h['media'] == 'bafkreigh2akiscaildc' and h['pos_ms'] == 0 and
              h['viewers'] == 1,
              'hall.load: stopped at 0 with the media')
        h = c.ok('hall.play', {'cid': cid, 'pos_ms': 60000, 'rate_q16': 131072})
        check(h['state'] == 'play' and h['rate_q16'] == 131072 and h['pos_ms'] >= 60000,
              'hall.play at 1:00, double speed')
        time.sleep(0.3)
        h2 = c.ok('hall.status', {'cid': cid})
        moved = h2['pos_ms'] - h['pos_ms']
        check(h2['state'] == 'play' and moved >= 2 * (h2['now_ms'] - h['now_ms']) - 4 and moved > 0,
              'hall.status: the playhead runs at 2x (%d ms)' % moved)
        h = c.ok('hall.pause', {'cid': cid})
        p = c.ok('hall.status', {'cid': cid})['pos_ms']
        time.sleep(0.1)
        check(h['state'] == 'pause' and c.ok('hall.status', {'cid': cid})['pos_ms'] == p,
              'hall.pause holds the playhead')
        h = c.ok('hall.seek', {'cid': cid, 'pos_ms': 5000})
        check(h['state'] == 'pause' and h['pos_ms'] == 5000, 'hall.seek keeps the state')
        e = err('hall.seek', {'cid': cid})
        check(e.get('code') == -32602, 'hall.seek needs pos_ms')
        e = err('hall.play', {'cid': cid, 'rate_q16': 0})
        check(e.get('code') == -32602, 'a zero rate is refused')
        posted = c.ok('pspace.post', {'cid': cid, 'text': 'hello "hall" \u00e9'})
        check(posted == {'cid': cid, 'posted': True}, 'pspace.post')
        ev = c.ok('pspace.events', {'cid': cid})
        kinds = [x['kind'] for x in ev['events']]
        texts = [x['text'] for x in ev['events'] if x['kind'] == 'post']
        check(kinds.count('hall') == 4 and texts == ['hello "hall" \u00e9'] and
              ev['next'] == ev['events'][-1]['index'],
              'pspace.events: four playback changes and the post, in order (%s)' % kinds)
        check(c.ok('pspace.events', {'cid': cid, 'since': ev['next']})['events'] == [],
              'pspace.events since the last index: nothing new')
        m = c.ok('pspace.members', {'cid': cid})
        check(m == {'members': [], 'count': 1}, 'pspace.members: only this node (%s)' % m)
        f = c.ok('pspace.find', {'topic': 'film'})
        check(f['topic'] == 'film' and isinstance(f['found'], list), 'pspace.find starts a search')
        e = err('pspace.find', {'topic': 'Film!'})
        check(e.get('code') == -32032, 'pspace.find: a bad topic is refused')
        e = err('pspace.join', {'cid': 'not-a-cid'})
        check(e.get('code') == -32032 and 'CID' in e.get('message', ''), 'pspace.join: a bad CID')
        e = err('pspace.post', {'cid': cid, 'text': ''})
        check(e.get('code') == -32602, 'pspace.post: empty text is refused')
        check(c.ok('pspace.leave', {'cid': cid}) == {'cid': cid, 'left': True}, 'pspace.leave')
        e = err('hall.status', {'cid': cid})
        check(e.get('code') == -32032 and 'not in' in e.get('message', ''), 'gone after leaving')
    finally:
        c.ok('net.command', {'command': 'off'})
        c.ok('features.set', {'feature': 'pspace', 'enabled': False})


def power_checks(c, generates):
    """power.* (zxvd_power.h W1-W9): the ledger, thermal.read, the companion as an agent
    under zxvd's own app, and an energy budget that makes it wait."""
    st = c.ok('power.status')
    check(st['host'] in ('linux', 'macos', 'windows') and st['device'] in ('desktop', 'laptop') and
          isinstance(st['consumers'], list) and st['consumers'][0]['kind'] == 'system' and
          st['token_scale_permille'] in (0, 382, 618, 1000),
          'power.status: the ledger as integer JSON (%s)' % {k: st[k] for k in ('host', 'device')})
    apps = c.ok('power.top', {'kind': 'app', 'n': 64})
    me = [a for a in apps if a['name'].lower().startswith('zxvd')]
    check(len(me) == 1 and me[0]['parent'] is None, 'power.top: zxvd is an app (%s)' % me)
    # W6: a figure only from a meter; with none, state neither and no number
    check(all(x['state'] in ('true', 'false', 'both', 'neither') and
              ((x['uj'] is None and x['mw'] is None) == (x['state'] in ('neither', 'false')))
              for x in apps), 'power.top: figures only where measured (%s)' %
          sorted({x['state'] for x in apps}))
    r = c.call('power.top', {'kind': 'apps'})
    check(r.get('error', {}).get('code') == -32602, 'power.top: an unknown kind is refused')
    # thermal.read (zxvd_power.h W9): this machine's real sensors, or NEITHER
    # with the reason; never a made-up number
    th = c.ok('thermal.read')
    accepted = [x for x in th['readings'] if x['state'] == 'true']
    check(th['state'] in ('true', 'false', 'both', 'neither') and
          set(th['classes']) == {'cpu', 'gpu', 'case', 'other'} and
          all(isinstance(x['mdeg'], int) and -50000 <= x['mdeg'] <= 150000 for x in accepted) and
          all(x['mdeg'] is None and x['reason'] for x in th['readings'] if x['state'] == 'false') and
          (th['state'] != 'neither' or (not th['readings'] and th['reason'])),
          'thermal.read: %s, %d readings (%s)' % (th['state'], len(th['readings']), th['reason']))
    r = c.call('power.budget', {'target': 'device', 'window_ms': 0, 'max_uj': 0})
    check(r.get('error', {}).get('code') == -32012, 'power.budget: off until switched on')
    c.ok('features.set', {'feature': 'power', 'on': True})
    check(c.ok('power.budget', {'target': 'device', 'window_ms': 0, 'max_uj': 0}) == {'ok': True},
          'power.budget: the device budget cleared')
    r = c.call('power.budget', {'target': 'agent', 'agent': 99999, 'window_ms': 1, 'max_uj': 1})
    check(r.get('error', {}).get('code') == -32095, 'power.budget: an unknown agent is refused')
    if not generates or not me:
        return
    # the companion's model call was noted; it shows after the next settle
    ag = []
    for _ in range(40):
        ag = [a for a in c.ok('power.top', {'kind': 'agent', 'lifetime': True})
              if a['name'] == 'companion']
        if ag and ag[0]['cpu_ns'] > 0:
            break
        time.sleep(0.1)
    check(len(ag) == 1 and ag[0]['parent'] == me[0]['id'] and ag[0]['cpu_ns'] > 0,
          'power.top: the companion is an agent under zxvd with its CPU time (%s)' % ag)
    if not ag:
        return
    comp = c.ok('companion.dna')['agent_id']
    if ag[0]['source'] == 'none':
        # nothing measured on this host: a strict budget holds the agent
        check(c.ok('power.budget', {'target': 'agent', 'agent': comp, 'window_ms': 600000,
                                    'max_uj': 1, 'strict': True}) == {'ok': True},
              'power.budget: a strict 1 uJ budget for the companion')
        a = c.ok('swarm.ask', {'prompt': 'hello again'})['answer']
        check(a.startswith('Energy budget reached'), 'swarm.ask waits on the budget: %r' % a[:80])
        check(c.ok('power.budget', {'target': 'agent', 'agent': comp, 'window_ms': 0,
                                    'max_uj': 0}) == {'ok': True},
              'power.budget: cleared (the unit test shows it admits again)')


def people_checks(c, binary, models, env, gguf):
    """social.*, call.*, xlate.*, devmesh.*, ui.* (zxvd_people.h H1-H9),
    including a call between two zxvd processes relayed through call.signal
    and the window's /api/rpc bridge."""
    def err(method, params=None):
        return c.call(method, params).get('error', {})

    names = set(c.ok('rpc.discover')['methods'])
    people = {'social.post', 'social.feed', 'social.groups', 'social.group_create', 'social.join',
              'social.leave', 'social.react', 'social.export', 'social.import', 'call.start',
              'call.accept', 'call.hangup', 'call.status', 'call.signal', 'xlate.detect',
              'xlate.translate', 'xlate.languages', 'devmesh.status', 'devmesh.invite',
              'devmesh.join', 'devmesh.confirm_sas', 'devmesh.revoke', 'devmesh.devices', 'ui.get',
              'ui.set'}
    check(people <= names, 'rpc.discover lists the %d people methods' % len(people))
    # R3.1 simple mode: off by default, shown only
    check(c.ok('ui.get') == {'simple_mode': False}, 'ui.get: simple mode is off by default')
    check(c.ok('ui.set', {'simple_mode': True}) == {'simple_mode': True}, 'ui.set: on')
    check(err('ui.set', {'simple_mode': 'yes'}).get('code') == -32602, 'ui.set: a string -32602')
    check(err('ui.set').get('code') == -32602, 'ui.set: nothing to set -32602')
    check(c.ok('ledger.check')['ok'] is True, 'simple mode changes no check: ledger.check still runs')
    check(c.ok('ui.set', {'simple_mode': False}) == {'simple_mode': False}, 'ui.set: off again')
    # the feed
    check(err('social.post', {'text': ''}).get('code') == -32602, 'social.post: empty -32602')
    check(err('social.post', {'text': 'x' * 1025}).get('code') == -32602, 'social.post: 1025 bytes')
    pid = c.ok('social.post', {'text': 'First light over the #harbour'})['id']
    check(len(pid) == 64, 'social.post: a signed post (%s)' % pid[:16])
    check('id' in c.ok('social.react', {'post': pid, 'reaction': '+1'}), 'social.react')
    check(err('social.react', {'post': '0' * 64, 'reaction': '+1'}).get('code') == -32030,
          'social.react: an unknown post -32030')
    f = c.ok('social.feed', {'limit': 5})
    it = f['items'][0] if f['items'] else {}
    check(it.get('id') == pid and it.get('mine') is True and
          it.get('reactions') == [{'reaction': '+1', 'count': 1, 'mine': True}] and
          it.get('surplus', 0) > 0, 'social.feed: the post with its reaction (%s)' % it)
    check(err('social.feed', {'limit': 0}).get('code') == -32602, 'social.feed: limit 0 -32602')
    g = c.ok('social.group_create', {'name': 'Harbour walkers'})
    gs = c.ok('social.groups')['groups']
    check(len(gs) == 1 and gs[0]['id'] == g['group'] and gs[0]['my_role'] == 'owner' and
          gs[0]['members'] == 1, 'social.groups: the new group, owned (%s)' % gs)
    check(err('social.join', {'group': g['group']}).get('code') == -32030,
          'social.join: already a member -32030')
    check(err('social.leave', {'group': g['group']}).get('code') == -32030,
          'social.leave: the owner cannot leave')
    ex = c.ok('social.export', {'id': pid})
    check(ex['bytes'] > 5000 and len(ex['hex']) == 2 * ex['bytes'],
          'social.export: the record with its key (%d bytes)' % ex['bytes'])
    check(c.ok('social.import', {'hex': ex['hex']})['added'] is False,
          'social.import of a held record: not added twice')
    bad = ex['hex'][:200] + ('0' if ex['hex'][200] != '0' else '1') + ex['hex'][201:]
    check(err('social.import', {'hex': bad}).get('code') == -32030,
          'social.import: a changed byte does not verify')
    # calls: loopback inside this zxvd
    st = c.ok('call.status')
    check('signalling only' in st['media'] and len(st['lines']) == 5,
          'call.status: signalling only, media is the host\'s job (%s)' % st['media'])
    r = c.ok('call.start', {'peer': 'loopback'})
    check(r['state'] == 'ringing_out', 'call.start loopback rings (%s)' % r['state'])
    check(c.ok('call.accept', {'line': 4})['state'] == 'connected', 'call.accept on the loopback line')
    check(c.ok('call.status')['lines'][0]['state'] == 'connected', 'the caller is connected')
    check(c.ok('call.hangup', {'line': 0})['end_reason'] == 'local_hangup', 'call.hangup')
    check(err('call.accept', {'line': 9}).get('code') == -32602, 'call.accept: line 9 -32602')
    check(err('call.hangup', {'line': 1}).get('code') == -32030, 'call.hangup: no call -32030')
    check(err('call.start', {'peer': 0}).get('code') == -32602, 'call.start: peer 0 -32602')
    check(err('call.signal', {'in': ['zz']}).get('code') == -32602, 'call.signal: bad hex -32602')
    # translation and language id
    d = c.ok('xlate.detect', {'text': 'Dies ist ein kurzer deutscher Satz über das Wetter.'})
    check(d['language'] == 'de' and d['reliable'] is True, 'xlate.detect: German (%s)' % d)
    check(c.ok('xlate.languages')['count'] > 20, 'xlate.languages')
    t = c.call('xlate.translate', {'text': 'Good morning, my friends.', 'to': 'fr'})
    if gguf:
        check('result' in t or t.get('error', {}).get('code') in (-32030, -32031),
              'xlate.translate with a model: a translation or a clear refusal (%s)' % t)
    else:
        check(t.get('error', {}).get('code') == -32031 and 'no model' in t['error']['message'],
              'xlate.translate with no model: -32031, nothing invented (%s)' % t.get('error'))
    same = c.ok('xlate.translate', {'text': 'Hello', 'from': 'en', 'to': 'en'})
    check(same['translated'] is False and same['text'] == 'Hello', 'same language: the original')
    check(err('xlate.translate', {'text': 'Hi', 'to': '!'}).get('code') == -32602,
          'xlate.translate: a bad tag -32602')
    # the device mesh
    ds = c.ok('devmesh.status')
    check(ds['joined'] is False and ds['role'] == 'home' and len(ds['self']) == 32,
          'devmesh.status: not in a mesh yet')
    inv = c.ok('devmesh.invite', {'role': 'thin'})
    check(inv['created_mesh'] is True and len(inv['code']) == 11 and inv['code'][5] == '-' and
          len(inv['qr']) > 40, 'devmesh.invite: QR payload and short code %s' % inv['code'])
    dv = c.ok('devmesh.devices')['devices']
    check(len(dv) == 1 and dv[0]['self'] is True and dv[0]['admin'] is True,
          'devmesh.devices: this computer, admin')
    check(err('devmesh.join', {'code': inv['code']}).get('code') == -32030,
          'devmesh.join: a node already in a mesh cannot join another')
    check(err('devmesh.join', {}).get('code') == -32602, 'devmesh.join: no code -32602')
    check(err('devmesh.confirm_sas', {'device': '00' * 16, 'match': True}).get('code') == -32030,
          'devmesh.confirm_sas: no pairing waiting')
    check(err('devmesh.revoke', {'device': ds['self']}).get('code') == -32030,
          'devmesh.revoke: not this device itself')
    check(err('devmesh.revoke', {'device': 'xyz'}).get('code') == -32602, 'devmesh.revoke: bad id')

    # a second zxvd: a call between two processes, relayed through call.signal
    home2 = tempfile.mkdtemp(prefix='zxvd-test2-')
    sock2 = os.path.join(home2, 'run', 'zxvd.sock')
    env2 = dict(env, HOME=home2, XDG_DATA_HOME=os.path.join(home2, 'xdg'))
    p2 = subprocess.Popen([binary, '--rpc', sock2, '--models-dir', models], stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, text=True, env=env2)
    try:
        url = wait_line(p2, 'ZXV-URL: ')
        check(wait_line(p2, 'ZXVD-RPC: ') == sock2, 'a second zxvd starts')
        c2 = Client(sock2)
        b_id = c2.ok('call.status')['self_id']
        a_line = c.ok('call.start', {'peer': b_id})['line']

        def relay(src, dst):
            moved = 0
            for _ in range(10):
                out = src.ok('call.signal')['out']
                back = dst.ok('call.signal', {'in': out})['out'] if out else []
                if back:
                    src.ok('call.signal', {'in': back})
                moved += len(out) + len(back)
                if not out and not back:
                    break
            return moved

        relay(c, c2)
        b_line = [x for x in c2.ok('call.status')['lines'] if x['state'] == 'incoming']
        check(len(b_line) == 1 and b_line[0]['peer'] == c.ok('call.status')['self_id'],
              'the second zxvd rings with a call from the first')
        if b_line:
            c2.ok('call.accept', {'line': b_line[0]['line']})
            relay(c2, c)
            relay(c, c2)
            check(c.ok('call.status')['lines'][a_line]['state'] == 'connected' and
                  c2.ok('call.status')['lines'][b_line[0]['line']]['state'] == 'connected',
                  'two zxvd processes connected through relayed signalling')
            c.ok('call.hangup', {'line': a_line})
            relay(c, c2)
            check(c2.ok('call.status')['lines'][b_line[0]['line']]['end_reason'] == 'remote_hangup',
                  'the hangup reaches the other zxvd')
        # the window's bridge: the token, a people method, a refused one
        if url and '#token=' in url:
            import http.client
            import urllib.parse
            u = urllib.parse.urlsplit(url)
            tok = u.fragment.split('token=', 1)[1]

            def post(body, token=tok):
                h = http.client.HTTPConnection(u.hostname, u.port, timeout=10)
                hdr = {'Content-Type': 'application/json'}
                if token:
                    hdr['X-ZXV-Token'] = token
                h.request('POST', '/api/rpc', body=json.dumps(body), headers=hdr)
                r = h.getresponse()
                out = (r.status, r.read())
                h.close()
                return out

            st, body = post({'jsonrpc': '2.0', 'id': 3, 'method': 'ui.get'})
            check(st == 200 and json.loads(body)['result'] == {'simple_mode': False},
                  '/api/rpc answers ui.get with the window token')
            st, body = post({'jsonrpc': '2.0', 'id': 4, 'method': 'zxvd.shutdown'})
            check(st == 403, '/api/rpc refuses methods outside its allow-list (%d)' % st)
            st, body = post({'jsonrpc': '2.0', 'id': 5, 'method': 'ui.get'}, token=None)
            check(st == 403, '/api/rpc refuses a request without the token (%d)' % st)
        else:
            check(False, 'zxvd prints its window URL with the token (%s)' % url)
        c2.ok('zxvd.shutdown')
        c2.close()
        p2.wait(timeout=10)
    finally:
        if p2.poll() is None:
            p2.kill()
            p2.wait()
        shutil.rmtree(home2, ignore_errors=True)


def main(binary, gguf=None):
    out = subprocess.run([binary, '--version'], capture_output=True, text=True, timeout=10)
    check(out.returncode == 0 and out.stdout.startswith('zxvd '), '--version names zxvd')
    out = subprocess.run([binary], capture_output=True, text=True, timeout=10)
    check(out.returncode == 2 and '--rpc' in out.stderr, 'refuses to start without --rpc')
    bad = os.path.join(tempfile.mkdtemp(prefix='zxvd-bad-'), 'run', 'zxvd.sock')
    out = subprocess.run([binary, '--rpc', bad, '--earnings-rate', '0'], capture_output=True,
                         text=True, timeout=10)
    check(out.returncode == 2 and '--earnings-rate' in out.stderr, 'refuses earnings rate 0')
    for code in ('VFV', 'vfv'):
        out = subprocess.run([binary, '--rpc', bad, '--earnings-ccy', code], capture_output=True,
                             text=True, timeout=10)
        check(out.returncode == 2 and 'never earns vouchers' in out.stderr and
              not os.path.exists(bad),
              'refuses %s for own-device credit (zxvd_rpc.h E6), leaving no socket' % code)
    for code in ('swc', 'USD'):
        out = subprocess.run([binary, '--rpc', bad, '--earnings-ccy', code], capture_output=True,
                             text=True, timeout=20)
        check(out.returncode == 1 and 'earnings currency' in out.stderr and
              not os.path.exists(bad),
              'refuses %s when the books open (libzxv code rule, or money), leaving no socket'
              % code)

    home = tempfile.mkdtemp(prefix='zxvd-test-')
    models = os.path.join(home, 'models')
    os.mkdir(models)
    if gguf:
        shutil.copy(gguf, os.path.join(models, 'tiny-test.gguf'))
    run = os.path.join(home, 'run')
    sock = os.path.join(run, 'zxvd.sock')
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'))
    if WINDOWS:  # the state folder is %APPDATA%\ZXV\net there; keep it in the test's home
        env['APPDATA'] = os.path.join(home, 'appdata')
        os.mkdir(env['APPDATA'])

    # a path that exists and is not a socket is never replaced
    os.mkdir(run, 0o700)
    with open(sock, 'w') as f:
        f.write('keep me')
    out = subprocess.run([binary, '--rpc', sock, '--models-dir', models], capture_output=True,
                         text=True, timeout=20, env=env)
    check(out.returncode != 0 and open(sock).read() == 'keep me',
          'a regular file at the socket path is refused, not replaced')
    os.unlink(sock)
    os.rmdir(run)

    # zxvd's stderr is kept and shown if a check fails: it says why a peer
    # or a request was refused
    err = open(os.path.join(home, 'zxvd.err'), 'w')
    proc = subprocess.Popen([binary, '--rpc', sock, '--models-dir', models, '--exit-with-parent'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=err, text=True, env=env)
    try:
        got = wait_line(proc, 'ZXVD-RPC: ')
        check(got == sock, 'zxvd announces its socket')
        if got != sock:
            return
        if WINDOWS:
            ok, aces = owner_only_acl(sock)
            check(ok, 'the socket file\'s DACL is this user only: %s' % aces)
            ok, aces = owner_only_acl(run)
            check(ok, 'its folder is created with this user only: %s' % aces)
        else:
            st = os.stat(sock)
            check(stat.S_ISSOCK(st.st_mode) and stat.S_IMODE(st.st_mode) == 0o600,
                  'socket is mode 0600 (%o)' % stat.S_IMODE(st.st_mode))
            check(stat.S_IMODE(os.stat(run).st_mode) == 0o700, 'its folder is created 0700')

        c = Client(sock)
        d = c.ok('rpc.discover')
        want = {'zxvd.version', 'zxvd.shutdown', 'swarm.state', 'swarm.ask', 'swarm.organ',
                'model.info', 'notes.list', 'notes.mark_read', 'net.command', 'update.check',
                'currency.enable', 'ledger.open_account', 'ledger.post', 'ledger.balance',
                'ledger.accounts', 'ledger.entries', 'ledger.check', 'swarm.earnings',
                'mesh.books', 'books.file', 'net.peers', 'net.offers', 'player.card',
                'jurisdiction.get', 'jurisdiction.set', 'chat.me', 'chat.post', 'chat.react',
                'chat.follow', 'chat.feed', 'chat.thread', 'iso20022.render', 'card.check',
                'netting.open', 'netting.add', 'netting.submit', 'netting.close',
                'netting.positions', 'netting.list', 'netting.drop', 'passkey.register',
                'passkey.verify', 'ledger.hold', 'ledger.release', 'ledger.refund',
                'ledger.holds', 'companion.dna', 'companion.generation', 'companion.mood',
                'companion.name', 'companion.name_preview', 'companion.name_confirm',
                'seats.list', 'seats.add', 'seats.remove', 'seats.set_lock',
                'seats.unlock', 'seats.lock', 'seats.auto', 'seats.set_auto', 'power.status',
                'power.top', 'power.budget', 'thermal.read', 'pspace.create', 'pspace.find', 'pspace.join',
                'pspace.leave', 'pspace.post', 'pspace.events', 'pspace.members', 'pspace.list',
                'hall.load', 'hall.play', 'hall.pause', 'hall.seek', 'hall.status'}
        if not WINDOWS:
            want |= {'aipi.put', 'aipi.get', 'aipi.publish', 'aipi.execute', 'aipi.status'}
        check(d['protocol'] == 'zxvd-rpc/1' and want <= set(d['methods']),
              'rpc.discover lists every method')
        v = c.ok('zxvd.version')
        maj, minor = (int(x) for x in v['libzxv'].split('.')[:2])
        check(v['protocol'] == 'zxvd-rpc/1' and maj == 1 and minor >= 2 and v['app'],
              'zxvd.version: app %s, libzxv %s through the ABI' % (v['app'], v['libzxv']))

        # ---- the swarm, live in the same process ----
        s0 = c.ok('swarm.state')
        check(s0['companion']['cycle_open'] and s0['ledger_halt'] == 0 and s0['levels'] >= 1,
              'swarm.state: companion cycle open, settlement spine running')
        t0 = time.time()
        s1 = c.ok('swarm.state')
        while (s1['cycle'] < 2 or s1['tick'] <= s0['tick']) and time.time() - t0 < 20:
            time.sleep(0.2)
            s1 = c.ok('swarm.state')
        check(s1['tick'] > s0['tick'] and s1['cycle'] >= 2,
              'the swarm keeps stepping while served over RPC (tick %d -> %d, cycle %d)' %
              (s0['tick'], s1['tick'], s1['cycle']))
        check(s1['held'] == 0 and s1['settled'] == s1['cycle'] and s1['ledger_halt'] == 0 and
              s1['ledger_synced'] == 2 * s1['cycle'],
              'every cycle settled and reconciled with the ledger of record '
              '(%d cycles, %d syncs)' % (s1['cycle'], s1['ledger_synced']))
        transfers = s1['ledger_postings'] - s1['ledger_issues']
        check(s1['audit'] == 'active' and s1['ledger_audited'] == transfers,
              'the node self-audits every ledger transfer (%d of %d, %s)' %
              (s1['ledger_audited'], transfers, s1['audit']))
        # ---- the swarm's earnings in the owner's books (zxvd_rpc.h E1-E4) ----
        ea = c.ok('swarm.earnings')
        check(ea['currency'] == 'SWC' and ea['rate_minor'] == '1' and
              ea['vfv_reversed_minor'] == '0' and
              int(ea['cycles']) >= 1 and int(ea['postings']) >= 1 and
              int(ea['balance_minor']) > 0,
              'swarm.earnings: settled cycles post own-device credit in SWC, never VFV (%s)' % ea)
        ob = c.ok('ledger.balance', {'account': ea['owner_account']})
        oj = c.ok('ledger.entries', {'account': ea['owner_account']})['entries']
        check(int(ob['equity_minor']) > 0 and
              sum(int(e['signed_minor']) for e in oj) == int(ob['equity_minor']) and
              all(e['counterparty'] == ea['source_account'] for e in oj) and
              [e['reference'] for e in oj][:2] == ['swarm earnings 1', 'swarm earnings 2'][:len(oj)],
              'the owner\'s SWC journal is the market\'s postings (%d entries)' % len(oj))
        # ---- AIPI on Z-Wasm (zxvd_aipi.h Z1-Z4); not in the Windows build yet ----
        if WINDOWS:
            r = c.call('aipi.status')
            check(r.get('error', {}).get('code') == -32601, 'aipi.*: not available on Windows')
            r = c.call('play.catalogue')
            check(r.get('error', {}).get('code') == -32601, 'play.*: not available on Windows')
        else:
            aipi_checks(c, home)
            play_checks(c)
        # ---- the people features (zxvd_people.h); not in the Windows build yet ----
        if WINDOWS:
            r = c.call('ui.get')
            check(r.get('error', {}).get('code') == -32601, 'people methods: not on Windows yet')
        else:
            people_checks(c, binary, models, env, gguf)
        econ_checks(c)
        companion_checks(c)
        name_checks(c, env)
        web4_checks(c)
        space_checks(c)
        mi = c.ok('model.info')
        if gguf:
            check(mi['loaded'] and mi['arch'] == 'qwen2' and mi['n_vocab'] > 1000,
                  'model.info: the GGUF is loaded (%s)' % mi['status'])
            a = c.ok('swarm.ask', {'prompt': 'hello there, swarm'})['answer']
            check(isinstance(a, str) and a, 'swarm.ask answers: %r' % a[:80])
            s2 = c.ok('swarm.state')
            comp = s2['companion']
            if mi['can_generate']:
                tag = comp.get('last_tag', {})
                check(tag.get('tokens', 0) > 0 and tag.get('base') == 'true' and
                      comp.get('quarantined') == 0,
                      'the answer was generated under the budget gate and tagged: %s' % tag)
                check(comp.get('answered_by_model') is True,
                      'swarm.state says the model wrote the last answer')
            else:
                check('tokens' in a, 'without the forward pass the gate still reports tokens')
                check(comp.get('answered_by_model') is False,
                      'swarm.state says the model did not write the last answer')
            power_checks(c, mi['can_generate'])
        else:
            check(not mi['loaded'], 'model.info: no model is a normal state')
            a = c.ok('swarm.ask', {'prompt': 'plan(trip) -> verify(costs)'})['answer']
            check('(plan(trip) -> verify(costs))' in a, 'swarm.ask reads shorthand back')
            o = c.ok('swarm.organ', {'organ': 'smith', 'prompt': 'plan(trip) -> verify(costs)'})
            check(o['organ'] == 'smith' and '(plan(trip) -> verify(costs))' in o['answer'],
                  'swarm.organ: the smith answers: %r' % o)
            check(o['verdict'] == 'neither' and not o['witnessed'] and o['action'] == 'wait',
                  'swarm.organ: no fitted head, so no verdict and no witness: %r' % o)
            check(c.ok('swarm.state')['companion'].get('answered_by_model') is False,
                  'with no model, swarm.state says no model wrote the answer')
            power_checks(c, False)
        r = c.call('swarm.organ', {'organ': 'blacksmith', 'prompt': 'x'})
        check(r.get('error', {}).get('code') == -32602, 'swarm.organ with an unknown organ: -32602')
        r = c.call('swarm.ask', {'prompt': 42})
        check(r.get('error', {}).get('code') == -32602, 'swarm.ask with a non-string prompt: -32602')
        r = c.call('net.command', {'command': 'everything'})
        check(r.get('error', {}).get('code') == -32602, 'unknown net command refused')
        n = c.ok('notes.list')
        check('notes' in n and 'unread' in n, 'notes.list')
        check(c.ok('notes.mark_read')['marked'] >= 0, 'notes.mark_read')

        # ---- the Studio panels' views (zxvd_rpc.h V1-V3) ----
        check(c.ok('net.peers') == {'peers': [], 'count': 0}, 'net.peers: none while off')
        check(c.ok('net.command', {'command': 'share 5 0'})['result'] != 'refused', 'share 5 compute')
        o = c.ok('net.offers')
        check(o == {'share': {'compute': '5', 'storage': '0'}, 'wants': [], 'outstanding': []},
              'net.offers: the share, no wants or receipts (%s)' % o)
        c.ok('net.command', {'command': 'share 0 0'})
        pc = c.ok('player.card')
        check(pc['tier'] == 0 and pc['metal'] == 'Copper' and pc['stone'] == 'Obsidian' and
              pc['perks'] == [] and pc['next']['metal'] == 'Bronze' and
              'bond' in pc['next']['missing'] and pc['inputs']['bond']['value'] == '0',
              'player.card: Copper, Bronze next, bond missing (%s)' % pc)
        card_practice_checks(c, pc)
        j = c.ok('jurisdiction.get')
        check(j['market'] == 'closed' and j['standard'] == '' and j['in_effect'] is True and j['plugins'] == 0 and j['report_standard'] == '' and
              'AAOIFI' in j['standards'], 'jurisdiction.get: closed clearing by default')

        # ---- the owner's chat (zxvd_chat.h C1-C8), in memory without --books ----
        me = c.ok('chat.me')
        check(me['saved'] is False and len(me['node_id']) == 64 and me['records'] == 0,
              'chat.me: a fresh in-memory identity (%s)' % me)
        post = c.ok('chat.post', {'text': 'hello from zxvd \u00e9'})
        check(post['kind'] == 'post' and post['mine'] is True and
              post['text'] == 'hello from zxvd \u00e9' and post['author'] == me['node_id'] and
              int(post['wall_ms']) > 0, 'chat.post: a signed post (%s)' % post)
        reply = c.ok('chat.post', {'text': 'a reply', 'reply_to': post['id']})
        check(reply['reply_to'] == post['id'], 'chat.post: a reply')
        c.ok('chat.react', {'to': post['id'], 'emoji': '\U0001F44D'})
        th = c.ok('chat.thread', {'id': post['id']})
        check(th['root']['replies'] == 1 and th['root']['reactions'] ==
              [{'emoji': '\U0001F44D', 'count': 1, 'mine': True}] and
              [r['id'] for r in th['replies']] == [reply['id']],
              'chat.thread: the reply and the reaction (%s)' % th)
        feed = c.ok('chat.feed')['posts']
        check([p['id'] for p in feed] == [post['id']], 'chat.feed: posts only (%s)' % feed)
        e = c.call('chat.post', {'text': '   '})
        check(e.get('error', {}).get('code') == -32602, 'chat.post: blank text is refused')

        seats_checks(c)

        # ---- libzxv's books, through the capability ----
        check(c.ok('currency.enable', {'code': 'USD'})['enabled'] == 'USD', 'currency.enable USD')
        bank = c.ok('ledger.open_account', {'name': 'bank', 'currency': 'USD', 'kind': 'issuer'})
        alice = c.ok('ledger.open_account', {'name': 'alice', 'currency': 'USD'})
        bob = c.ok('ledger.open_account', {'name': 'bob', 'currency': 'USD', 'kind': 'holder'})
        B, A, O = bank['account'], alice['account'], bob['account']
        check(len({B, A, O}) == 3, 'three accounts opened: %s' % [B, A, O])
        e1 = c.ok('ledger.post', {'from': B, 'to': A, 'amount_minor': 10000, 'currency': 'USD',
                                  'reference': 'E2E-ISSUE-1'})
        e2 = c.ok('ledger.post', {'from': A, 'to': O, 'amount_minor': 2550, 'currency': 'USD',
                                  'reference': 'E2E-PAY-1'})
        check(int(e2['entry_id']) > int(e1['entry_id']) and isinstance(e1['entry_id'], str),
              'postings get increasing entry ids (64-bit values as decimal strings)')
        ba = c.ok('ledger.balance', {'account': A})
        bo = c.ok('ledger.balance', {'account': O})
        bb = c.ok('ledger.balance', {'account': B})
        check(ba['equity_minor'] == '7450' and bo['equity_minor'] == '2550' and
              bb['equity_minor'] == '-10000',
              'balances are exact: alice 7450, bob 2550, bank -10000')
        r = c.call('ledger.post', {'from': O, 'to': A, 'amount_minor': 2551, 'currency': 'USD',
                                   'reference': 'E2E-OVERDRAW'})
        check(r.get('error', {}).get('code') == -32000 and
              r['error'].get('data') == {'status': -6, 'name': 'ZXV_E_FUNDS', 'class': 'REFUSED'},
              'an overdraft is refused: ZXV_E_FUNDS, class REFUSED')
        r = c.call('ledger.post', {'from': A, 'to': O, 'amount_minor': 1.5, 'currency': 'USD',
                                   'reference': 'E2E-FRACTION'})
        check(r.get('error', {}).get('code') == -32602, 'a fractional amount is -32602, never rounded')
        r = c.call('ledger.post', {'from': A, 'to': O, 'amount_minor': 1, 'currency': 'USD',
                                   'reference': 'E2E-PAY-1'})
        check(r.get('error', {}).get('data', {}).get('name') == 'ZXV_E_DUPLICATE',
              'a reused reference is refused with ZXV_E_DUPLICATE')
        e3 = c.ok('ledger.post', {'from': O, 'to': A, 'amount_minor': '50', 'currency': 'USD',
                                  'reference': 'E2E-STR-1'})
        check(int(e3['entry_id']) == int(e2['entry_id']) + 1 and
              c.ok('ledger.balance', {'account': O})['equity_minor'] == '2500',
              'a decimal-string amount is accepted')
        c.ok('ledger.post', {'from': A, 'to': O, 'amount_minor': 50, 'currency': 'USD',
                             'reference': 'E2E-STR-2'})
        ent = [e for e in c.ok('ledger.entries', {'account': A})['entries']
               if not e['reference'].startswith('E2E-STR')]
        check([e['signed_minor'] for e in ent] == ['10000', '-2550'] and
              ent[1]['counterparty'] == O and ent[1]['reference'] == 'E2E-PAY-1',
              'ledger.entries: alice\'s journal')
        acc = c.ok('ledger.accounts')['accounts']
        usd = [a for a in acc if a['currency'] == 'USD']
        swc = [a for a in acc if a['currency'] == 'SWC']
        check([a['name'] for a in usd] == ['bank', 'alice', 'bob'] and usd[0]['kind'] == 'issuer' and
              not [a for a in acc if a['currency'] == 'VFV'] and
              [(a['name'], a['kind']) for a in swc] ==
              [('swarm market', 'issuer'), ('owner: swarm earnings', 'holder')],
              'ledger.accounts')
        check(c.ok('ledger.check')['ok'] is True, 'ledger.check: the books balance')
        check_modules(c, A)

        payment_checks(c, A)

        # ---- protocol ----
        c.send_raw('{not json\n')
        check(c.read()['error']['code'] == -32700, 'parse error: -32700')
        c.send_raw('[{"jsonrpc":"2.0","id":1,"method":"zxvd.version"}]\n')
        check(c.read()['error']['code'] == -32600, 'batch: -32600')
        r = c.call('no.such.method')
        check(r['error']['code'] == -32601, 'unknown method: -32601')
        c.send_raw('{"jsonrpc":"2.0","id":7,"method":"ledger.balance","params":[1]}\n')
        check(c.read()['error']['code'] == -32602, 'positional params: -32602')
        c.send_raw('{"jsonrpc":"2.0","method":"notes.mark_read"}\n')
        r = c.call('zxvd.version')
        check('result' in r, 'a notification gets no reply (the next reply is the next call\'s)')
        c.send_raw('{"jsonrpc":"2.0","id":"s-1","method":"zxvd.version"}\r\n')
        r = c.read()
        check(r.get('id') == 's-1' and 'result' in r, 'string ids and CRLF lines')

        others = [Client(sock) for _ in range(3)]
        check(all(o.ok('swarm.state')['tick'] >= s1['tick'] for o in others),
              'several clients are served at once')
        big = Client(sock)
        try:
            # the server may close mid-send (EPIPE on macOS): that is the expected outcome
            big.send_raw('{"jsonrpc":"2.0","id":1,"method":"swarm.ask","params":{"prompt":"' +
                         'x' * 20000 + '"}}\n')
            big.read()
            closed = False
        except (ConnectionError, OSError):
            closed = True
        check(closed, 'an over-long line closes that client')
        check('result' in others[0].call('zxvd.version'), 'other clients are unaffected')
        for o in others:
            o.close()

        p2 = subprocess.run([binary, '--rpc', sock, '--models-dir', models], capture_output=True,
                            text=True, timeout=20, env=env)
        check(p2.returncode != 0 and 'another zxvd' in p2.stderr,
              'a second zxvd cannot take a live socket')
        check('result' in c.call('ledger.check'), 'the first zxvd is still serving')

        check(c.ok('zxvd.shutdown')['stopping'] is True, 'zxvd.shutdown accepted')
        c.close()
        proc.wait(timeout=10)
        check(proc.returncode == 0, 'zxvd exits cleanly')
        check(not os.path.exists(sock), 'the socket file is removed')

        if not WINDOWS:  # a logout or kill: SIGTERM stops it as cleanly
            p3 = subprocess.Popen([binary, '--rpc', sock, '--models-dir', models],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                  env=env)
            try:
                check(wait_line(p3, 'ZXVD-RPC: ') == sock, 'a new zxvd takes the socket')
                p3.terminate()
                p3.wait(timeout=10)
                check(p3.returncode == 0 and not os.path.exists(sock),
                      'SIGTERM: zxvd exits cleanly and removes its socket')
            finally:
                if p3.poll() is None:
                    p3.kill()
                    p3.wait()

        # ---- the books file (zxvd_rpc.h F1-F6): the books outlive a restart ----
        books = os.path.join(home, 'data', 'books.zxvb')
        os.makedirs(os.path.dirname(books), exist_ok=True)

        def run_with_books():
            p = subprocess.Popen([binary, '--rpc', sock, '--models-dir', models, '--books', books],
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                 env=env)
            check(wait_line(p, 'ZXVD-RPC: ') == sock, 'zxvd starts with --books')
            return p, Client(sock)

        def stop(p, cl):
            cl.ok('zxvd.shutdown')
            cl.close()
            p.wait(timeout=10)
            check(p.returncode == 0, 'zxvd with --books exits cleanly')

        p4, c4 = run_with_books()
        try:
            name_after_restart(c4)
            bf = c4.ok('books.file')
            check(bf['saved'] is True and bf['writable'] is True and int(bf['records']) >= 2,
                  'books.file: saved, writable (%s)' % bf)
            if not WINDOWS:
                check(stat.S_IMODE(os.stat(books).st_mode) == 0o600, 'the books file is 0600')
            c4.ok('currency.enable', {'code': 'EUR'})
            fa = c4.ok('ledger.open_account', {'name': 'saved bank', 'currency': 'EUR',
                                               'kind': 'issuer'})['account']
            fb = c4.ok('ledger.open_account', {'name': 'saved cash', 'currency': 'EUR'})['account']
            fe = c4.ok('ledger.post', {'from': fa, 'to': fb, 'amount_minor': '4200',
                                       'currency': 'EUR', 'reference': 'E2E-SAVED-1'})['entry_id']
            t0 = time.time()
            while c4.ok('swarm.earnings')['owner_account'] is None and time.time() - t0 < 30:
                time.sleep(0.2)
            bal = c4.ok('swarm.earnings')['balance_minor']
            js = c4.ok('jurisdiction.set', {'market': 'open', 'standard': 'IFRS', 'country': 'KE'})
            check(js['market'] == 'open' and js['country'] == 'KE', 'jurisdiction.set')
            c4.ok('features.set', {'feature': 'escrow'})  # saved in the books file (X7)
            kh = c4.ok('ledger.hold', {'from': fa, 'to': fb, 'amount_minor': 700})
            seat = c4.ok('seats.add', {'name': 'Saved', 'lock': 'pass', 'pass': 'saved pass'})
            seats_file = os.path.join(os.path.dirname(books), 'seats')
            if not WINDOWS:
                check(stat.S_IMODE(os.stat(seats_file).st_mode) == 0o600,
                      'the seats file is 0600, beside the books')
            if not WINDOWS:
                c4.ok('ui.set', {'simple_mode': True})
                kept_post = c4.ok('social.post', {'text': 'saved beside the books'})['id']
                mesh = c4.ok('devmesh.invite')['mesh_id']
                ppl = os.path.join(os.path.dirname(books), 'people.zxvp')
                check(stat.S_IMODE(os.stat(ppl).st_mode) == 0o600 and
                      stat.S_IMODE(os.stat(ppl + '.mesh').st_mode) == 0o600,
                      'people.zxvp and its mesh snapshot sit beside the books, 0600')
            me4 = c4.ok('chat.me')
            kept = c4.ok('chat.post', {'text': 'kept across a restart'})
            check(me4['saved'] is True, 'chat.me: saved beside the books')
            if not WINDOWS:
                seed = os.path.join(os.path.dirname(books), 'social.seed')
                check(stat.S_IMODE(os.stat(seed).st_mode) == 0o600, 'the chat key file is 0600')
            stop(p4, c4)
            p4, c4 = run_with_books()
            if not WINDOWS:
                check(c4.ok('ui.get') == {'simple_mode': True}, 'simple mode survives a restart')
                check([x['id'] for x in c4.ok('social.feed')['items']] == [kept_post],
                      'the post survives a restart')
                ds = c4.ok('devmesh.status')
                check(ds['joined'] is True and ds['mesh_id'] == mesh,
                      'the device mesh survives a restart')
            got = c4.ok('ledger.entries', {'account': fb})['entries']
            check([(e['entry_id'], e['signed_minor'], e['reference']) for e in got] ==
                  [(fe, '4200', 'E2E-SAVED-1')], 'a restart brings the posting back (%s)' % got)
            fs4 = {f['name']: f for f in c4.ok('features.get')['features']}
            check(fs4['escrow']['enabled'] is True and fs4['netting']['enabled'] is False,
                  'a restart keeps the escrow switch the owner turned on, and only it')
            hs = c4.ok('ledger.holds')
            check([h for h in hs['holds'] if h['hold'] == kh['hold']] == [kh] and
                  hs['unmatched'] == 0 and all(a['consistent'] for a in hs['escrow_accounts']),
                  'a restart brings the escrow hold back from the books file (%s)' % hs)
            check(c4.ok('ledger.balance', {'account': fb})['equity_minor'] == '4200',
                  'and the balance')
            rel = c4.ok('ledger.release', {'hold': kh['hold']})
            check(rel['state'] == 'released' and
                  c4.ok('ledger.balance', {'account': fb})['equity_minor'] == '4900',
                  'and it is released after the restart (%s)' % rel)
            ea = c4.ok('swarm.earnings')
            check(ea['resumed'] is True and int(ea['balance_minor']) >= 0 and
                  ea['owner_account'] is not None,
                  'the earnings account comes back (%s; was %s)' % (ea, bal))
            mb = c4.ok('mesh.books')
            check(mb['currency'] == 'VFV' and mb['wallet_account'] is None,
                  'mesh.books: no peer sales yet (%s)' % mb)
            check(c4.ok('ledger.check')['ok'] is True, 'the replayed books balance')
            j = c4.ok('jurisdiction.get')
            check((j['market'], j['standard'], j['country']) == ('open', 'IFRS', 'KE'),
                  'the jurisdiction settings come back (%s)' % j)
            sl = c4.ok('seats.list')['seats']
            check([(s_['id'], s_['name'], s_['lock']) for s_ in sl] ==
                  [(seat['id'], 'Saved', 'pass')] and
                  c4.ok('seats.unlock', {'id': seat['id'], 'pass': 'saved pass'})['ok'] is True,
                  'a restart brings the seat back and its passphrase still opens it (%s)' % sl)
            me5 = c4.ok('chat.me')
            check(me5['node_id'] == me4['node_id'] and me5['records'] == 1 and
                  c4.ok('chat.thread', {'id': kept['id']})['root']['text'] ==
                  'kept across a restart', 'the chat identity and post come back (%s)' % me5)
            stop(p4, c4)
            with open(seats_file, 'rb') as f:
                kept_seats = f.read()
            with open(seats_file, 'wb') as f:
                f.write(kept_seats.replace(b'\nend\n', b'\n'))
            p6 = subprocess.run([binary, '--rpc', sock, '--models-dir', models, '--books', books],
                                capture_output=True, text=True, timeout=20, env=env)
            check(p6.returncode != 0 and 'seats file' in p6.stderr and not os.path.exists(sock),
                  'a damaged seats file stops zxvd from starting (%s)' % p6.stderr.strip()[-200:])
            with open(seats_file, 'wb') as f:
                f.write(kept_seats)
            with open(books, 'r+b') as f:  # an edit outside zxvd stops the next start
                data = f.read()
                f.seek(data.index(b'4200'))
                f.write(b'4300')
            p5 = subprocess.run([binary, '--rpc', sock, '--models-dir', models, '--books', books],
                                capture_output=True, text=True, timeout=20, env=env)
            check(p5.returncode != 0 and 'hash chain' in p5.stderr and not os.path.exists(sock),
                  'damaged books stop zxvd from starting (%s)' % p5.stderr.strip()[-200:])
        finally:
            if p4.poll() is None:
                p4.kill()
                p4.wait()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        err.close()
        if FAILS:
            with open(err.name) as f:
                print('zxvd stderr (exit %s):\n%s' % (proc.returncode, f.read()[-4000:]))
        shutil.rmtree(home, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    try:
        main(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else None)
    except Exception as e:  # an unexpected reply shape is a failure, not a crash
        check(False, 'unexpected: %r' % (e,))
    if FAILS:
        print('[FAIL] zxvd: %d check(s) failed' % len(FAILS))
        sys.exit(1)
    print('[PASS] zxvd end to end over JSON-RPC')
