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
socket; and zxvd.shutdown stops it and removes the socket file."""
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


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


class Client:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
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

    proc = subprocess.Popen([binary, '--rpc', sock, '--models-dir', models, '--exit-with-parent'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, env=env)
    try:
        got = wait_line(proc, 'ZXVD-RPC: ')
        check(got == sock, 'zxvd announces its socket')
        if got != sock:
            return
        st = os.stat(sock)
        check(stat.S_ISSOCK(st.st_mode) and stat.S_IMODE(st.st_mode) == 0o600,
              'socket is mode 0600 (%o)' % stat.S_IMODE(st.st_mode))
        check(stat.S_IMODE(os.stat(run).st_mode) == 0o700, 'its folder is created 0700')

        c = Client(sock)
        d = c.ok('rpc.discover')
        want = {'zxvd.version', 'zxvd.shutdown', 'swarm.state', 'swarm.ask', 'model.info',
                'notes.list', 'notes.mark_read', 'net.command', 'update.check',
                'currency.enable', 'ledger.open_account', 'ledger.post', 'ledger.balance',
                'ledger.accounts', 'ledger.entries', 'ledger.check', 'swarm.earnings'}
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
        else:
            check(not mi['loaded'], 'model.info: no model is a normal state')
            a = c.ok('swarm.ask', {'prompt': 'plan(trip) -> verify(costs)'})['answer']
            check('(plan(trip) -> verify(costs))' in a, 'swarm.ask reads shorthand back')
        r = c.call('swarm.ask', {'prompt': 42})
        check(r.get('error', {}).get('code') == -32602, 'swarm.ask with a non-string prompt: -32602')
        r = c.call('net.command', {'command': 'everything'})
        check(r.get('error', {}).get('code') == -32602, 'unknown net command refused')
        n = c.ok('notes.list')
        check('notes' in n and 'unread' in n, 'notes.list')
        check(c.ok('notes.mark_read')['marked'] >= 0, 'notes.mark_read')

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
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
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
