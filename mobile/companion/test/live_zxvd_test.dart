// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The app's own link and store against a real zxvd (set ZXVD_BIN to it):
// turn the phone link on over the control socket, pair, and drive every
// view's calls through zxv-phone/1. Skipped without ZXVD_BIN.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/src/json.dart';
import 'package:zxv_companion/src/link.dart';
import 'package:zxv_companion/src/pairing.dart';
import 'package:zxv_companion/src/store.dart';

final bin = Platform.environment['ZXVD_BIN'];

/// One request over zxvd's UNIX control socket (what the Mac app does).
Future<Map<String, dynamic>> control(String sock, String method, [Map<String, dynamic>? params]) async {
  final s = await Socket.connect(InternetAddress(sock, type: InternetAddressType.unix), 0);
  s.write('${jsonEncode({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': ?params})}\n');
  final line = await s.cast<List<int>>().transform(utf8.decoder).transform(const LineSplitter()).first;
  s.destroy();
  return jsonDecode(line) as Map<String, dynamic>;
}

void main() {
  late Directory home;
  late Process proc;
  late String sock;

  setUpAll(() async {
    if (bin == null) return;
    home = await Directory.systemTemp.createTemp('zxvd-companion-');
    sock = '${home.path}/run/zxvd.sock';
    proc = await Process.start(bin!, ['--rpc', sock], environment: {'HOME': home.path});
    final ready = Completer<void>();
    proc.stdout.transform(utf8.decoder).transform(const LineSplitter()).listen((l) {
      if (l.startsWith('ZXVD-RPC: ') && !ready.isCompleted) ready.complete();
    });
    proc.stderr.drain<void>();
    await ready.future.timeout(const Duration(seconds: 30));
  });

  tearDownAll(() async {
    if (bin == null) return;
    try {
      await control(sock, 'zxvd.shutdown');
    } catch (_) {}
    await proc.exitCode.timeout(const Duration(seconds: 20), onTimeout: () {
      proc.kill();
      return -1;
    });
    await home.delete(recursive: true);
  });

  test('pair, then every view\'s calls over the phone link', () async {
    final on = await control(sock, 'phone.link', {'on': true, 'port': 0, 'bind': '127.0.0.1'});
    expect(on['result']['on'], true);
    final r = (await control(sock, 'phone.pair', {'name': 'Test phone'}))['result'] as Map<String, dynamic>;
    final p = Pairing.parse(r['link'] as String).withHost('127.0.0.1', r['port'] as int);
    expect(p.name, 'Test phone');

    final link = await PhoneLink.connect(p);
    expect(J(await link.call('zxvd.version')).str('protocol'), 'zxvd-rpc/1');
    // several calls queued at once come back in order, each to its caller
    final many = await Future.wait([for (var i = 0; i < 20; i++) link.call(i.isEven ? 'swarm.state' : 'ledger.check')]);
    expect(J(many[0]).integer('cores'), greaterThan(0));
    expect(J(many[1]).boolean('ok'), true);
    await expectLater(link.call('zxvd.shutdown'), throwsA(isA<RpcError>().having((e) => e.notAllowed, 'notAllowed', true)));
    await expectLater(
        link.call('ledger.post', {'from': 0, 'to': 1, 'amount_minor': 1, 'currency': 'VFV', 'reference': 'x'}),
        throwsA(isA<RpcError>().having((e) => e.toString(), 'message', contains('read-only'))));
    link.close();

    // the earnings accounts open on the swarm's first settled cycle
    for (var i = 0; i < 60; i++) {
      final a = (await control(sock, 'ledger.accounts'))['result']['accounts'] as List;
      if (a.isNotEmpty) break;
      await Future<void>.delayed(const Duration(milliseconds: 500));
    }
    final store = AppStore(pairings: MemoryPairingStore(), pollEvery: const Duration(hours: 1));
    expect(await store.pair(p.toLink()), isNull);
    expect(store.link, LinkState.connected);
    expect(store.has('swarm.ask'), true);
    expect(store.state.integer('tokens_per_cycle'), greaterThan(0));
    expect(store.accounts, isNotEmpty); // the swarm earnings accounts
    expect(store.earnings.str('currency'), 'SWC'); // own-device books post in SWC, never VFV
    await store.ask('hello from the phone');
    expect(store.chat.last.mine, false);
    expect(store.chat.last.text, isNot(contains('could not answer')));
    final posted = await store.post(1, '1.00', 'phone test');
    expect(posted.$1, false);
    expect(posted.$2, contains('read-only'));
    expect(await store.net('lan'), false);
    await store.select(store.accounts.first.integer('account'));
    expect(store.balance.str('equity_minor'), isNotEmpty);

    // a pairing allowed to post moves money from the phone
    final r2 = (await control(sock, 'phone.pair', {'name': 'Owner phone', 'allow_post': true}))['result'];
    final store2 = AppStore(pairings: MemoryPairingStore(), pollEvery: const Duration(hours: 1));
    expect(await store2.pair(Pairing.parse(r2['link'] as String).withHost('127.0.0.1', r2['port'] as int).toLink()), isNull);
    expect((await store2.enableCurrency('USD')).$1, true);
    expect((await store2.openAccount('Bank', 'USD', 'issuer')).$1, true);
    final bank = store2.selectedAccount!;
    expect((await store2.openAccount('Wallet', 'USD', 'holder')).$1, true);
    final wallet = store2.selectedAccount!;
    await store2.select(bank);
    final ok = await store2.post(wallet, '12.34', 'top-up');
    expect(ok.$1, true, reason: ok.$2);
    await store2.select(wallet);
    expect(store2.balance.str('equity_minor'), '1234');
    expect((await store2.post(bank, '0.001', 'x')).$2, contains('2 decimal places'));

    // revoking on the Mac cuts the phone off, and it cannot come back
    await control(sock, 'phone.revoke', {'phone_id': p.idHex});
    await store.refresh();
    expect(store.link, LinkState.down);
    await expectLater(PhoneLink.connect(p), throwsA(anything));
    store.dispose();
    store2.dispose();
  }, skip: bin == null ? 'set ZXVD_BIN to a zxvd binary' : false, timeout: const Timeout(Duration(minutes: 3)));
}
