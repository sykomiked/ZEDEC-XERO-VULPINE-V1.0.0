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


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
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


def wait_line(proc, prefix, secs=20):
    t0 = time.time()
    while time.time() - t0 < secs:
        line = proc.stdout.readline()
        if not line:
            return None
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return None


# sdk/kits/aipi/test/aipi_mods.h, assembled: half(n) = n / 2, its inverse
# half_ok(n, r), and spin(n), a loop that only fuel stops; and "floaty",
# half(n) through an f64 that the Z-Wasm validator refuses.
HALVES = ('0061736d01000000010c0260017e017e60027e7e017f0304030001000719030468616c66000007'
          '68616c665f6f6b0001047370696e00020a2a030700200042027f0b1600200142027e2000512001'
          '42027e42017c200051720b090003400c000b42000b')
FLOATY = '0061736d0100000001060160017e017e030201000708010468616c6600000a080106002000b9b00b'
HALVES_ZC = 'fn half(n: int[0..1000000]) -> int[0..500000];\n'


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


def power_checks(c, generates):
    """power.* (zxvd_power.h W1-W8): the ledger, the companion as an agent
    under zxvd's own app, and an energy budget that makes it wait."""
    st = c.ok('power.status')
    check(st['host'] in ('linux', 'macos', 'windows') and st['device'] in ('desktop', 'laptop') and
          isinstance(st['consumers'], list) and st['consumers'][0]['kind'] == 'system' and
          st['token_scale_permille'] in (0, 382, 618, 1000),
          'power.status: the ledger as integer JSON (%s)' % {k: st[k] for k in ('host', 'device')})
    apps = c.ok('power.top', {'kind': 'app', 'n': 64})
    me = [a for a in apps if a['name'].lower().startswith('zxvd')]
    check(len(me) == 1 and me[0]['parent'] is None, 'power.top: zxvd is an app (%s)' % me)
    r = c.call('power.top', {'kind': 'apps'})
    check(r.get('error', {}).get('code') == -32602, 'power.top: an unknown kind is refused')
    r = c.call('power.budget', {'target': 'device', 'window_ms': 0, 'max_uj': 0})
    check(r.get('error', {}).get('code') == -32012, 'power.budget: off until switched on')
    c.ok('features.set', {'feature': 'power', 'on': True})
    check(c.ok('power.budget', {'target': 'device', 'window_ms': 0, 'max_uj': 0}) == {'ok': True},
          'power.budget: the device budget cleared')
    r = c.call('power.budget', {'target': 'agent', 'agent': 99999, 'window_ms': 1, 'max_uj': 1})
    check(r.get('error', {}).get('code') == -32100, 'power.budget: an unknown agent is refused')
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


def main(binary, gguf=None):
    out = subprocess.run([binary, '--version'], capture_output=True, text=True, timeout=10)
    check(out.returncode == 0 and out.stdout.startswith('zxvd '), '--version names zxvd')
    out = subprocess.run([binary], capture_output=True, text=True, timeout=10)
    check(out.returncode == 2 and '--rpc' in out.stderr, 'refuses to start without --rpc')
    bad = os.path.join(tempfile.mkdtemp(prefix='zxvd-bad-'), 'run', 'zxvd.sock')
    out = subprocess.run([binary, '--rpc', bad, '--earnings-rate', '0'], capture_output=True,
                         text=True, timeout=10)
    check(out.returncode == 2 and '--earnings-rate' in out.stderr, 'refuses earnings rate 0')
    out = subprocess.run([binary, '--rpc', bad, '--earnings-ccy', 'vfv'], capture_output=True,
                         text=True, timeout=20)
    check(out.returncode == 1 and 'earnings currency' in out.stderr and not os.path.exists(bad),
          'refuses an earnings currency libzxv does not accept, leaving no socket')

    home = tempfile.mkdtemp(prefix='zxvd-test-')
    models = os.path.join(home, 'models')
    os.mkdir(models)
    if gguf:
        shutil.copy(gguf, os.path.join(models, 'tiny-test.gguf'))
    run = os.path.join(home, 'run')
    sock = os.path.join(run, 'zxvd.sock')
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'xdg'))

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
        want = {'zxvd.version', 'zxvd.shutdown', 'swarm.state', 'swarm.ask', 'model.info',
                'notes.list', 'notes.mark_read', 'net.command', 'update.check',
                'currency.enable', 'ledger.open_account', 'ledger.post', 'ledger.balance',
                'ledger.accounts', 'ledger.entries', 'ledger.check', 'swarm.earnings',
                'mesh.books', 'books.file', 'net.peers', 'net.offers', 'player.card',
                'jurisdiction.get', 'jurisdiction.set', 'chat.me', 'chat.post', 'chat.react',
                'chat.follow', 'chat.feed', 'chat.thread', 'iso20022.render', 'card.check',
                'netting.open', 'netting.add', 'netting.submit', 'netting.close',
                'netting.positions', 'netting.list', 'netting.drop', 'passkey.register',
                'passkey.verify', 'ledger.hold', 'ledger.release', 'ledger.refund',
                'ledger.holds', 'companion.dna', 'companion.generation', 'companion.mood',
                'seats.list', 'seats.add', 'seats.remove', 'seats.set_lock',
                'seats.unlock', 'seats.lock', 'seats.auto', 'seats.set_auto', 'power.status',
                'power.top', 'power.budget'}
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
        check(ea['currency'] == 'VFV' and ea['rate_minor'] == '1' and
              int(ea['cycles']) >= 1 and int(ea['postings']) >= 1 and
              int(ea['balance_minor']) > 0,
              'swarm.earnings: settled cycles post into the owner\'s books in VFV (%s)' % ea)
        ob = c.ok('ledger.balance', {'account': ea['owner_account']})
        oj = c.ok('ledger.entries', {'account': ea['owner_account']})['entries']
        check(int(ob['equity_minor']) > 0 and
              sum(int(e['signed_minor']) for e in oj) == int(ob['equity_minor']) and
              all(e['counterparty'] == ea['source_account'] for e in oj) and
              [e['reference'] for e in oj][:2] == ['swarm earnings 1', 'swarm earnings 2'][:len(oj)],
              'the owner\'s VFV journal is the market\'s postings (%d entries)' % len(oj))
        # ---- AIPI on Z-Wasm (zxvd_aipi.h Z1-Z4); not in the Windows build yet ----
        if WINDOWS:
            r = c.call('aipi.status')
            check(r.get('error', {}).get('code') == -32601, 'aipi.*: not available on Windows')
        else:
            aipi_checks(c, home)
        econ_checks(c)
        companion_checks(c)
        web4_checks(c)
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
            else:
                check('tokens' in a, 'without the forward pass the gate still reports tokens')
            power_checks(c, mi['can_generate'])
        else:
            check(not mi['loaded'], 'model.info: no model is a normal state')
            a = c.ok('swarm.ask', {'prompt': 'plan(trip) -> verify(costs)'})['answer']
            check('(plan(trip) -> verify(costs))' in a, 'swarm.ask reads shorthand back')
            power_checks(c, False)
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
        j = c.ok('jurisdiction.get')
        check(j['market'] == 'closed' and j['standard'] == '' and j['in_effect'] is False and
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
        vfv = [a for a in acc if a['currency'] == 'VFV']
        check([a['name'] for a in usd] == ['bank', 'alice', 'bob'] and usd[0]['kind'] == 'issuer' and
              [(a['name'], a['kind']) for a in vfv] ==
              [('swarm market', 'issuer'), ('owner: swarm earnings', 'holder')],
              'ledger.accounts')
        check(c.ok('ledger.check')['ok'] is True, 'ledger.check: the books balance')

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
            me4 = c4.ok('chat.me')
            kept = c4.ok('chat.post', {'text': 'kept across a restart'})
            check(me4['saved'] is True, 'chat.me: saved beside the books')
            if not WINDOWS:
                seed = os.path.join(os.path.dirname(books), 'social.seed')
                check(stat.S_IMODE(os.stat(seed).st_mode) == 0o600, 'the chat key file is 0600')
            stop(p4, c4)
            p4, c4 = run_with_books()
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
