// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/main.dart';
import 'package:zxv_companion/src/link.dart';
import 'package:zxv_companion/src/pairing.dart';
import 'package:zxv_companion/src/store.dart';

/// Canned zxvd answers, shaped like the real ones.
class FakeRpc implements Rpc {
  final calls = <String>[];
  @override
  bool isClosed = false;

  static final state = {
    'cores': 8, 'cores_milli': 6250, 'mem_total_mb': 131072, 'mem_mb': 65536, 'levels': 4,
    'tokens_per_cycle': 2378, 'mood': '\u{1F610} 2', 'ledger_halt': 0, 'ledger_postings': 57,
    'quest': {
      'contributed': 3,
      'skills': [
        {'skill': 'compute', 'level': 1, 'points': 9}
      ]
    },
    'agents': [
      {'id': 1, 'name': 'Companion', 'level': 0, 'emoji': '\u{1F610}', 'intensity': 5, 're': 353, 'mk': 188, 'im': 0,
        'witness': 1, 'witness_by': 'Translator-5'}
    ],
    'model': {'loaded': true, 'can_generate': true, 'name': 'qwen2.5-1.5b', 'arch': 'qwen2', 'layers': 28,
      'vocab': 151936, 'context': 4096, 'status': 'ready'},
    'companion': {'allotted': 590, 'remaining': 400, 'used': 190, 'max_per_answer': 256, 'asks': 1, 'quarantine_ok': true,
      'last_tag': {'base': 'True', 'polarity': 'plus', 'phase': 'settled', 'action': 'proceed'},
      'last_answer': {'charged': 12}},
    'net': {'mode': 'lan', 'bound': true, 'bind': '0.0.0.0', 'port': 8723, 'node_id': 'abcd', 'id': 'ab' * 32,
      'peers': 2, 'conserved': true},
    'update': {'status': 'up to date', 'checks': 1},
  };

  @override
  Future<dynamic> call(String method, [Map<String, dynamic>? params, Duration? timeout]) async {
    calls.add(method);
    switch (method) {
      case 'rpc.discover':
        return {'protocol': 'zxvd-rpc/1', 'methods': ['swarm.state', 'swarm.ask', 'ledger.post']};
      case 'zxvd.version':
        return {'app': '0.1.0-preview', 'protocol': 'zxvd-rpc/1', 'libzxv': '1.4.0'};
      case 'swarm.state':
        return state;
      case 'notes.list':
        return {'unread': 1, 'notes': [{'seq': 1, 'title': 'Model ready', 'body': '', 'kind': 'info', 'source': 'zxvd', 'read': false, 'count': 1}]};
      case 'ledger.accounts':
        return {'accounts': [
          {'account': 0, 'name': 'swarm market', 'currency': 'VFV', 'kind': 'issuer'},
          {'account': 1, 'name': 'owner: swarm earnings', 'currency': 'VFV', 'kind': 'holder'},
        ]};
      case 'ledger.balance':
        return {'debit_minor': '0', 'credit_minor': '0', 'equity_minor': '10998'};
      case 'ledger.entries':
        return {'entries': [{'entry_id': '1', 'signed_minor': '10998', 'counterparty': 1, 'reference': 'swarm earnings 1'}]};
      case 'ledger.check':
        return {'ok': true};
      case 'swarm.earnings':
        return {'currency': 'VFV', 'rate_minor': '1', 'balance_minor': '10998', 'postings': '2', 'cycles': '2'};
      case 'swarm.ask':
        return {'answer': 'Hello from the swarm.'};
      case 'ledger.post':
        throw RpcError(-32012, 'this phone was paired without allow_post');
    }
    throw RpcError(-32601, 'method not found');
  }

  @override
  void close() => isClosed = true;
}

final pairing = Pairing.parse('zxv-phone:1?h=192.168.1.9&p=47410&id=${'00' * 16}&k=${'11' * 32}&n=Pixel');

void main() {
  testWidgets('unpaired: the pairing screen explains a bad link', (tester) async {
    final store = AppStore(pairings: MemoryPairingStore(), connector: (_) async => FakeRpc());
    await store.start();
    await tester.pumpWidget(CompanionApp(store: store, allowScan: false));
    expect(find.text('Pair with your Mac'), findsOneWidget);
    await tester.enterText(find.byKey(const Key('link')), 'hello');
    await tester.tap(find.byKey(const Key('pair')));
    await tester.pumpAndSettle();
    expect(find.textContaining('Not a ZXV pairing link'), findsOneWidget);
    store.dispose();
  });

  testWidgets('paired: the five views render and the companion answers', (tester) async {
    tester.view.physicalSize = const Size(1170, 2532);
    tester.view.devicePixelRatio = 3;
    addTearDown(tester.view.reset);
    final rpc = FakeRpc();
    final store = AppStore(
        pairings: MemoryPairingStore(pairing), connector: (_) async => rpc, pollEvery: const Duration(days: 1));
    await store.start();
    await tester.pumpWidget(CompanionApp(store: store, allowScan: false));
    await tester.pumpAndSettle();
    expect(find.textContaining('Connected to 0.1.0-preview'), findsOneWidget);
    expect(find.text('Local AI Studio'), findsOneWidget);

    await tester.enterText(find.byKey(const Key('ask')), 'hi');
    await tester.tap(find.byKey(const Key('send')));
    await tester.pumpAndSettle();
    expect(find.text('Hello from the swarm.'), findsOneWidget);
    expect(find.text('12 tokens'), findsOneWidget);

    await tester.tap(find.text('Ledger'));
    await tester.pumpAndSettle();
    expect(find.text('Sicily Ledger'), findsOneWidget);
    expect(find.text('109.98 VFV'), findsWidgets);

    await tester.tap(find.text('Mesh'));
    await tester.pumpAndSettle();
    expect(find.text('P2P Mesh'), findsOneWidget);

    await tester.tap(find.text('Store'));
    await tester.pumpAndSettle();
    expect(find.text('Storefront & Escrow'), findsOneWidget);

    await tester.tap(find.text('Settings'));
    await tester.pumpAndSettle();
    expect(find.text('192.168.1.9:47410'), findsOneWidget);
    await tester.scrollUntilVisible(find.text('Model ready'), 300,
        scrollable: find.descendant(of: find.byType(ListView), matching: find.byType(Scrollable)).first);
    expect(find.text('Model ready'), findsOneWidget);
    expect(rpc.calls, isNot(contains('zxvd.shutdown')));
    store.dispose();
  });

  test('a read-only pairing is told so in plain words', () async {
    final store = AppStore(pairings: MemoryPairingStore(pairing), connector: (_) async => FakeRpc(),
        pollEvery: const Duration(days: 1));
    await store.start();
    final r = await store.post(0, '1.00', 'x');
    expect(r.$1, false);
    expect(r.$2, contains('paired read-only'));
    store.dispose();
  });
}
