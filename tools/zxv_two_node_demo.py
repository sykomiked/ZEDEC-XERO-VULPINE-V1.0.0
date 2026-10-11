#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""zxv_two_node_demo.py — two ZXV nodes: one asks, the other answers, and the
answer is paid for with the 0.08889% assurance fee.

  Node A (the asker) finds node B over UDP (Vinea, ML-DSA-87 signed), opens a
  TCP session to it (X25519 + ML-KEM-1024 key exchange, ML-DSA-87 on both
  sides), sends a prompt, gets B's answer with B's signed invoice, pays B the
  price plus the fee (split 50/25/15/10 into the four fee buckets) on its peer
  book, and gets B's signed acknowledgement. See docs/TWO_NODE_DEMO.md.

Three ways to run it (Python 3.8+, no packages):

  On ONE computer, both nodes (CI does this; also the quickest try):
      python3 tools/zxv_two_node_demo.py local
  On TWO laptops on the same Wi-Fi or LAN:
      laptop B:  python3 tools/zxv_two_node_demo.py serve
      laptop A:  python3 tools/zxv_two_node_demo.py ask 192.168.1.23
                 (the address B printed)

zxvd is found with --zxvd PATH, else $ZXVD, else built once into
~/.zxv-demo/build with kernel/arch/hosted/build_zxvd.sh (needs a C compiler).
Give B a model with --model FILE.gguf for real answers; without one the
swarm's stand-in agents answer.

`local` runs node A on 127.0.0.2 and node B on 127.0.0.3. Linux has the whole
127/8 on loopback. macOS has only 127.0.0.1 until you add the others:
      sudo ifconfig lo0 alias 127.0.0.2 up
      sudo ifconfig lo0 alias 127.0.0.3 up
`local --check` also runs the refusal cases and exits non-zero on any failure.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_PORT = 8723
DEFAULT_PRICE = 125000      # VFV minor units: 1,250.00 VFV per answer
DEFAULT_FLOAT = 1000000     # demo VFV in the asker's peer wallet: 10,000.00 VFV
FEE_NUM, FEE_DEN = 8889, 10 ** 7
FAILS = []


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        FAILS.append(msg)


def vfv(minor):
    minor = int(minor)
    return '%s%d.%02d VFV' % ('-' if minor < 0 else '', abs(minor) // 100, abs(minor) % 100)


# ----------------------------------------------------------------- zxvd ----

def find_zxvd(given):
    for cand in (given, os.environ.get('ZXVD')):
        if cand:
            if os.path.isfile(cand) and os.access(cand, os.X_OK):
                return cand
            sys.exit('zxvd not found at %s' % cand)
    out = os.path.join(os.path.expanduser('~'), '.zxv-demo', 'build')
    binary = os.path.join(out, 'zxvd')
    script = os.path.join(REPO, 'kernel', 'arch', 'hosted', 'build_zxvd.sh')
    if not os.path.exists(binary):
        if not os.path.exists(script):
            sys.exit('no zxvd: pass --zxvd PATH (or run from a checkout of the repository)')
        print('Building zxvd once into %s (a minute or two) ...' % out)
        r = subprocess.run(['sh', script, out])
        if r.returncode != 0 or not os.path.exists(binary):
            sys.exit('building zxvd failed (a C compiler is needed: Xcode command line tools on '
                     'a Mac, build-essential on Linux)')
    return binary


class Rpc:
    """JSON-RPC 2.0 over zxvd's UNIX socket, one request per line."""

    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(300)
        self.s.connect(path)
        self.buf = b''
        self.n = 0

    def call(self, method, params=None):
        self.n += 1
        req = {'jsonrpc': '2.0', 'id': self.n, 'method': method}
        if params is not None:
            req['params'] = params
        self.s.sendall((json.dumps(req) + '\n').encode())
        while b'\n' not in self.buf:
            k = self.s.recv(65536)
            if not k:
                raise ConnectionError('zxvd closed the socket')
            self.buf += k
        line, _, self.buf = self.buf.partition(b'\n')
        return json.loads(line)

    def ok(self, method, params=None):
        r = self.call(method, params)
        if 'result' not in r:
            raise RuntimeError('%s: %s' % (method, r.get('error', {}).get('message', r)))
        return r['result']


class Node:
    """One zxvd process with its own home, socket and network address."""

    def __init__(self, name, binary, bind, port, extra):
        self.name = name
        self.home = tempfile.mkdtemp(prefix='zxv-node-%s-' % name.lower())
        self.sock = os.path.join(self.home, 'run', 'zxvd.sock')
        env = dict(os.environ, HOME=self.home, XDG_DATA_HOME=os.path.join(self.home, 'xdg'))
        cmd = [binary, '--rpc', self.sock, '--net', 'lan', '--net-bind', bind,
               '--net-port', str(port), '--exit-with-parent'] + extra
        if not any(a == '--model' or a == '--models-dir' for a in extra):
            os.mkdir(os.path.join(self.home, 'models'))
            cmd += ['--models-dir', os.path.join(self.home, 'models')]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, env=env)
        self.port = None
        self.node_id = None
        self.serving = None
        self.log = []
        t0 = time.time()
        while time.time() - t0 < 60:
            line = self.proc.stdout.readline()
            if not line:
                break
            self.log.append(line.rstrip())
            if line.startswith('Network: lan, UDP '):
                addr, _, node = line[len('Network: lan, UDP '):].partition(', node ')
                self.port = int(addr.rsplit(':', 1)[1])
                self.node_id = node.strip()
            elif line.startswith('Peers: answering'):
                self.serving = line.strip()
            elif 'not serving peers' in line or 'network not started' in line:
                self.serving = line.strip()
            elif line.startswith('ZXVD-RPC: '):
                break
        if self.port is None:
            self.stop()
            raise RuntimeError('node %s did not start its network:\n  %s'
                               % (name, '\n  '.join(self.log[-8:])))
        self.rpc = Rpc(self.sock)

    def stop(self):
        try:
            if getattr(self, 'rpc', None):
                self.rpc.call('zxvd.shutdown')
        except (OSError, ConnectionError, ValueError):
            pass
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()
        shutil.rmtree(self.home, ignore_errors=True)


def can_bind(ip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind((ip, 0))
        return True
    except OSError:
        return False
    finally:
        s.close()


def lan_ip():
    """This machine's address on the LAN (no packet is sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.168.255.255', 9))
        return s.getsockname()[0]
    except OSError:
        return '127.0.0.1'
    finally:
        s.close()


def show_settlement(res):
    st = res['settlement']
    parts = st['fee_parts']
    print('\nAnswer from %s...:\n  %s\n' % (st['peer'][:16], res['answer'].replace('\n', '\n  ')))
    print('Settlement (A pays B):')
    print('  price          %s' % vfv(st['price_minor']))
    print('  fee 0.08889%%   %s' % vfv(st['fee_minor']))
    for k, v in parts.items():
        print('    %-22s %s' % (k, vfv(v)))
    print('  A paid         %s' % vfv(st['paid_minor']))
    print('  invoice        %s' % st['invoice'])
    print('  settle         %s' % st['settle'])
    print('  security       %s' % res['security'])


# ---------------------------------------------------------------- modes ----

def mode_local(a):
    binary = find_zxvd(a.zxvd)
    ip_a, ip_b = '127.0.0.2', '127.0.0.3'
    if not (can_bind(ip_a) and can_bind(ip_b)):
        sys.exit('this computer has only 127.0.0.1 on loopback. On a Mac run once:\n'
                 '  sudo ifconfig lo0 alias 127.0.0.2 up\n  sudo ifconfig lo0 alias 127.0.0.3 up')
    extra_b = ['--peer-serve', str(a.price)] + (['--model', a.model] if a.model else [])
    print('Starting node B on %s (answers for %s) and node A on %s ...'
          % (ip_b, vfv(a.price), ip_a))
    b = Node('B', binary, ip_b, 0, extra_b)
    nodes = [b]
    try:
        n_a = Node('A', binary, ip_a, 0, ['--peer-float', str(a.float)])
        nodes.append(n_a)
        print('  B: UDP+TCP %s:%d, node %s' % (ip_b, b.port, b.node_id))
        print('  A: UDP %s:%d, node %s' % (ip_a, n_a.port, n_a.node_id))
        peer = '%s:%d' % (ip_b, b.port)
        t0 = time.time()
        res = n_a.rpc.call('peer.ask', {'peer': peer, 'prompt': a.prompt,
                                        'max_price_minor': a.max_price})
        took = time.time() - t0
        if 'result' not in res:
            print('peer.ask failed: %s' % res.get('error'))
            return 1
        show_settlement(res['result'])
        print('  round trip     %.2f s (UDP discovery, PQ handshake, answer, settlement)' % took)
        sa, sb = n_a.rpc.ok('peer.status'), b.rpc.ok('peer.status')
        print('\nBooks after: A wallet %s, B wallet %s; both books verify: %s'
              % (vfv(sa['wallet_minor']), vfv(sb['wallet_minor']),
                 sa['book_ok'] and sb['book_ok']))
        if a.check:
            run_checks(a, n_a, b, res['result'], sa, sb)
    finally:
        for n in reversed(nodes):
            n.stop()
    if a.check:
        if FAILS:
            print('[FAIL] two-node demo: %d failure(s)' % len(FAILS))
            return 1
        print('[PASS] two-node demo: UDP discovery, PQ TCP session, remote answer, fee settlement')
    return 0


def run_checks(a, n_a, b, res, sa, sb):
    print('\nChecks:')
    st = res['settlement']
    price = a.price
    fee = price * FEE_NUM // FEE_DEN
    check(st['peer'].startswith(b.node_id), "the answer is signed by B's NodeID")
    check(int(st['price_minor']) == price and int(st['fee_minor']) == fee,
          'fee is floor(price * 8889 / 10^7) = %d' % fee)
    pcts = (25, 15, 10)
    rest = [fee * p // 100 for p in pcts]
    want = [fee - sum(rest)] + rest
    check([int(v) for v in st['fee_parts'].values()] == want,
          'fee split 50/25/15/10, remainder to the reserve floor: %s' % want)
    check(int(sa['wallet_minor']) == a.float - price - fee, "A's wallet paid price + fee")
    check(int(sb['wallet_minor']) == price, 'B received the whole price')
    check([int(v) for v in sa['buckets'].values()] == want, "A's four fee buckets hold the fee")
    check(sa['book_ok'] and sb['book_ok'], 'both peer books verify (invariants and hash chain)')
    check(sa['bought'] == 1 and sb['sold'] == 1, 'one purchase on A, one sale on B')
    hb = sb['history'][-1] if sb['history'] else {}
    check(hb.get('invoice') == st['invoice'] and hb.get('settle') == st['settle'] and
          hb.get('session') == st['session'], 'both nodes hold the same invoice, settle and session')
    check(not sa['serving'] and sb['serving'], 'only B listens on TCP (serving is opt-in)')
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3)
    refused = s.connect_ex(('127.0.0.2', n_a.port)) != 0
    s.close()
    check(refused, "nothing accepts TCP on A's port while A is not serving")

    r = n_a.rpc.call('peer.ask', {'peer': '127.0.0.3:%d' % b.port, 'prompt': 'cheap?',
                                  'max_price_minor': price - 1})
    check('error' in r and r['error']['code'] == -32010 and 'price' in r['error']['message'],
          'max_price below the price: refused (%s)' % r.get('error', {}).get('message'))
    after = n_a.rpc.ok('peer.status')
    check(after['wallet_minor'] == sa['wallet_minor'], 'nothing was paid for the refusal')

    r = b.rpc.call('peer.ask', {'peer': '127.0.0.2:%d' % n_a.port, 'prompt': 'hello',
                                'max_price_minor': 10 ** 12})
    check('error' in r and 'wallet' in r['error']['message'],
          "B's wallet cannot cover max_price: nothing is sent (%s)"
          % r.get('error', {}).get('message'))
    r = b.rpc.call('peer.ask', {'peer': '127.0.0.2:%d' % n_a.port, 'prompt': 'hello',
                                'max_price_minor': 10})
    check('error' in r and 'serving' in r['error']['message'],
          'asking a node that is not serving fails cleanly (%s)'
          % r.get('error', {}).get('message'))
    r = n_a.rpc.call('peer.ask', {'peer': '8.8.8.8', 'prompt': 'x', 'max_price_minor': 1})
    check('error' in r, 'LAN mode: a public address is refused')
    r = n_a.rpc.call('peer.ask', {'peer': '127.0.0.3:%d' % b.port, 'prompt': 'x',
                                  'max_price_minor': 1.5})
    check('error' in r and r['error']['code'] == -32602, 'a fractional price is a params error')

    # A's listener comes and goes with peer.serve
    srv = n_a.rpc.ok('peer.serve', {'price_minor': 1})
    check(srv['serving'] and srv['port'] == n_a.port, 'peer.serve opens A\'s TCP listener')
    off = n_a.rpc.ok('peer.serve', {})
    check(not off['serving'], 'peer.serve without a price stops it')
    # a second paid round: the books keep adding up
    r = n_a.rpc.ok('peer.ask', {'peer': '127.0.0.3:%d' % b.port, 'prompt': 'again',
                                'max_price_minor': price})
    sa2, sb2 = n_a.rpc.ok('peer.status'), b.rpc.ok('peer.status')
    check(int(sb2['wallet_minor']) == 2 * price and sb2['sold'] == 2,
          'a second request: B now holds twice the price')
    check(int(sa2['wallet_minor']) == a.float - 2 * price - int(sa2['fees_paid_minor']) and
          sa2['book_ok'], "A's wallet = float - 2 prices - fees, book verifies")


def mode_serve(a):
    binary = find_zxvd(a.zxvd)
    extra = ['--peer-serve', str(a.price)] + (['--model', a.model] if a.model else [])
    n = Node('B', binary, a.bind, a.port, extra)
    ip = lan_ip() if a.bind == '0.0.0.0' else a.bind
    print('Node B is up. %s' % (n.serving or ''))
    print('  node id   %s...' % n.node_id)
    print('  price     %s per answer (plus the 0.08889%% fee, paid by the asker)' % vfv(a.price))
    print('\nOn the other laptop run:\n  python3 tools/zxv_two_node_demo.py ask %s%s\n'
          % (ip, '' if n.port == DEFAULT_PORT else ':%d' % n.port))
    print('If nothing arrives, allow incoming connections for zxvd (macOS: System Settings >'
          ' Network > Firewall) on UDP and TCP port %d.  Ctrl-C stops.' % n.port)
    try:
        seen = 0
        while n.proc.poll() is None:
            time.sleep(1)
            st = n.rpc.ok('peer.status')
            for h in st['history'][seen:]:
                print('  sold an answer to %s...: +%s (book verifies: %s)'
                      % (h['peer'][:16], vfv(h['price_minor']), st['book_ok']))
            seen = len(st['history'])
    except KeyboardInterrupt:
        pass
    finally:
        n.stop()
    return 0


def mode_ask(a):
    binary = find_zxvd(a.zxvd)
    host = a.peer if ':' in a.peer else '%s:%d' % (a.peer, DEFAULT_PORT)
    n = Node('A', binary, a.bind, a.port, ['--peer-float', str(a.float)])
    try:
        print('Node A is up (node %s...), wallet %s demo VFV. Asking %s ...'
              % (n.node_id[:16], vfv(a.float), host))
        r = n.rpc.call('peer.ask', {'peer': host, 'prompt': a.prompt,
                                    'max_price_minor': a.max_price})
        if 'result' not in r:
            print('Failed: %s' % r.get('error', {}).get('message'))
            return 1
        show_settlement(r['result'])
        st = n.rpc.ok('peer.status')
        print('\nA wallet now %s; book verifies: %s' % (vfv(st['wallet_minor']), st['book_ok']))
    finally:
        n.stop()
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('--zxvd', help='path to a zxvd binary')
    p.add_argument('--model', help='GGUF model for the answering node')
    sub = p.add_subparsers(dest='mode', required=True)
    lo = sub.add_parser('local', help='both nodes on this computer (127.0.0.2 and 127.0.0.3)')
    lo.add_argument('--check', action='store_true', help='also run the refusal cases (CI)')
    sv = sub.add_parser('serve', help='node B: answer prompts for a price')
    sv.add_argument('--bind', default='0.0.0.0')
    sv.add_argument('--port', type=int, default=DEFAULT_PORT)
    ak = sub.add_parser('ask', help='node A: ask node B and pay it')
    ak.add_argument('peer', help="B's address, IP or IP:PORT")
    ak.add_argument('--bind', default='0.0.0.0')
    ak.add_argument('--port', type=int, default=DEFAULT_PORT)
    for sp in (lo, sv, ak):
        sp.add_argument('--price', type=int, default=DEFAULT_PRICE, help='VFV minor units')
    for sp in (lo, ak):
        sp.add_argument('--max-price', type=int, default=2 * DEFAULT_PRICE)
        sp.add_argument('--float', type=int, default=DEFAULT_FLOAT,
                        help='demo VFV minor units in the asker\'s peer wallet')
        sp.add_argument('--prompt', default='In one sentence: why do two computers need a shared '
                                            'secret before they talk?')
    a = p.parse_args()
    if a.mode == 'local':
        a.max_price = max(a.max_price, a.price)
    return {'local': mode_local, 'serve': mode_serve, 'ask': mode_ask}[a.mode](a)


if __name__ == '__main__':
    sys.exit(main())
