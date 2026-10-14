// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Everything the five views show, read from zxvd over the phone link. The
// same calls as the Mac app's AppStore (macos/studio), minus what only the
// Mac may do (zxvd.shutdown, pairing).
import 'dart:async';

import 'package:flutter/foundation.dart';

import 'json.dart';
import 'link.dart';
import 'money.dart';
import 'pairing.dart';

/// What the store needs from a connection; [PhoneLink] in the app, a fake
/// in tests.
abstract class Rpc {
  Future<dynamic> call(String method, [Map<String, dynamic>? params, Duration? timeout]);
  bool get isClosed;
  void close();
}

class _LinkRpc implements Rpc {
  _LinkRpc(this.l);
  final PhoneLink l;
  @override
  Future<dynamic> call(String method, [Map<String, dynamic>? params, Duration? timeout]) =>
      l.call(method, params, timeout);
  @override
  bool get isClosed => l.isClosed;
  @override
  void close() => l.close();
}

typedef Connector = Future<Rpc> Function(Pairing p);

Future<Rpc> linkConnector(Pairing p) async => _LinkRpc(await PhoneLink.connect(p));

enum LinkState { unpaired, connecting, connected, down }

class ChatLine {
  ChatLine(this.mine, this.text, {this.tag, this.charged});
  final bool mine;
  final String text;
  final J? tag;
  final int? charged;
}

class LogLine {
  LogLine(this.command, this.result, this.ok);
  final String command, result;
  final bool ok;
}

/// (ok, line to show under a form)
typedef Outcome = (bool, String);

class AppStore extends ChangeNotifier {
  AppStore({required this.pairings, this.connector = linkConnector, this.pollEvery = const Duration(seconds: 3)});

  final PairingStore pairings;
  final Connector connector;
  final Duration pollEvery;

  LinkState link = LinkState.unpaired;
  String downReason = '';
  Pairing? pairing;
  Rpc? _rpc;
  Timer? _poll;
  bool _foreground = true;
  bool _connecting = false;

  Set<String> methods = {};
  J version = J.empty;
  J state = J.empty;
  List<J> notes = [];
  int unread = 0;
  J earnings = J.empty;

  List<J> accounts = [];
  int? selectedAccount;
  J balance = J.empty;
  List<J> entries = [];
  bool? booksOK;
  final List<String> currenciesEnabled = [];

  final List<ChatLine> chat = [];
  bool asking = false;
  final List<LogLine> netLog = [];

  bool has(String m) => methods.contains(m);

  J? get selected {
    for (final a in accounts) {
      if (a.integer('account', -1) == selectedAccount) return a;
    }
    return null;
  }

  String nameOf(int account) {
    for (final a in accounts) {
      if (a.integer('account', -1) == account) return a.str('name');
    }
    return '#$account';
  }

  // ---------- pairing and the connection ----------

  Future<void> start() async {
    pairing = await pairings.load();
    if (pairing == null) {
      link = LinkState.unpaired;
      notifyListeners();
      return;
    }
    await connect();
  }

  /// Pairs with a link from the Mac; the line to show, or null when paired.
  Future<String?> pair(String text) async {
    final Pairing p;
    try {
      p = Pairing.parse(text);
    } on FormatException catch (e) {
      return e.message;
    }
    pairing = p;
    await connect();
    if (link != LinkState.connected) {
      final why = downReason;
      pairing = null;
      link = LinkState.unpaired;
      notifyListeners();
      return 'Could not reach zxvd at ${p.host}:${p.port}: $why';
    }
    await pairings.save(p);
    return null;
  }

  Future<void> forget() async {
    _drop();
    await pairings.forget();
    pairing = null;
    link = LinkState.unpaired;
    chat.clear();
    netLog.clear();
    accounts = [];
    state = J.empty;
    notifyListeners();
  }

  /// The Mac moved to another address: keep the pairing, change where it is.
  Future<void> changeAddress(String host, int port) async {
    final p = pairing;
    if (p == null) return;
    pairing = p.withHost(host, port);
    await pairings.save(pairing!);
    _drop();
    await connect();
  }

  Future<void> connect() async {
    final p = pairing;
    if (p == null || _connecting) return;
    _connecting = true;
    link = LinkState.connecting;
    notifyListeners();
    try {
      _rpc = await connector(p);
      final d = J(await _rpc!.call('rpc.discover'));
      methods = d.strings('methods').toSet();
      version = J(await _rpc!.call('zxvd.version'));
      link = LinkState.connected;
      await refresh();
      await refreshLedger();
      earnings = J(await _safe('swarm.earnings'));
    } catch (e) {
      _drop();
      link = LinkState.down;
      downReason = '$e';
    } finally {
      _connecting = false;
      _schedule();
      notifyListeners();
    }
  }

  void _drop() {
    _rpc?.close();
    _rpc = null;
  }

  void _schedule() {
    _poll?.cancel();
    if (!_foreground || pairing == null) return;
    _poll = Timer.periodic(pollEvery, (_) => refresh());
  }

  /// The app went to the background (stop polling) or came back.
  void setForeground(bool on) {
    _foreground = on;
    if (on && link == LinkState.down) {
      connect();
    } else {
      _schedule();
    }
  }

  Future<dynamic> _call(String m, [Map<String, dynamic>? p, Duration? t]) async {
    final r = _rpc;
    if (r == null || r.isClosed) throw RpcError(0, 'not connected');
    try {
      return await r.call(m, p, t);
    } on RpcError catch (e) {
      if (e.code == 0) {
        _drop();
        link = LinkState.down;
        downReason = e.message;
        notifyListeners();
      }
      rethrow;
    }
  }

  Future<dynamic> _safe(String m, [Map<String, dynamic>? p]) async {
    try {
      return await _call(m, p);
    } catch (_) {
      return null;
    }
  }

  Future<void> refresh() async {
    if (link == LinkState.down) {
      await connect();
      return;
    }
    if (link != LinkState.connected) return;
    try {
      state = J(await _call('swarm.state'));
      final n = J(await _call('notes.list'));
      notes = n.list('notes');
      unread = n.integer('unread');
    } on RpcError {
      // a refused poll is not a lost link; a lost link was handled in _call
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _poll?.cancel();
    _drop();
    super.dispose();
  }

  // ---------- Local AI Studio ----------

  Future<void> ask(String prompt) async {
    final p = prompt.trim();
    if (p.isEmpty || asking) return;
    chat.add(ChatLine(true, p));
    asking = true;
    notifyListeners();
    try {
      final cut = p.length > 4095 ? p.substring(0, 4095) : p;
      final a = J(await _call('swarm.ask', {'prompt': cut}, const Duration(minutes: 5)));
      final s = J(await _safe('swarm.state'));
      if (!s.isNull) state = s;
      final c = s['companion'];
      chat.add(ChatLine(false, a.str('answer'),
          tag: c['last_tag'].isNull ? null : c['last_tag'],
          charged: c['last_answer'].isNull ? null : c['last_answer'].integer('charged')));
    } catch (e) {
      chat.add(ChatLine(false, 'zxvd could not answer: $e'));
    }
    asking = false;
    notifyListeners();
  }

  // ---------- Sicily Ledger ----------

  Future<void> refreshLedger() async {
    final a = J(await _safe('ledger.accounts'));
    if (a.isNull) return;
    accounts = a.list('accounts');
    for (final c in accounts.map((x) => x.str('currency'))) {
      if (c.isNotEmpty && !currenciesEnabled.contains(c)) currenciesEnabled.add(c);
    }
    if (selected == null) selectedAccount = accounts.isEmpty ? null : accounts.first.integer('account');
    await _loadSelected();
    final chk = J(await _safe('ledger.check'));
    if (!chk.isNull) booksOK = chk.boolean('ok');
    notifyListeners();
  }

  Future<void> select(int account) async {
    selectedAccount = account;
    await _loadSelected();
    notifyListeners();
  }

  Future<void> _loadSelected() async {
    final id = selectedAccount;
    if (id == null) {
      balance = J.empty;
      entries = [];
      return;
    }
    balance = J(await _safe('ledger.balance', {'account': id}));
    entries = J(await _safe('ledger.entries', {'account': id})).list('entries');
  }

  Future<Outcome> post(int to, String amount, String reference) async {
    final from = selected;
    if (from == null) return (false, 'Pick an account to send from.');
    final ccy = from.str('currency');
    final r = parseAmount(amount, ccy);
    if (r is ParseFailed) return (false, r.why);
    final minor = (r as Parsed).minor;
    final ref = reference.trim();
    try {
      final p = J(await _call('ledger.post', {
        'from': from.integer('account'),
        'to': to,
        'amount_minor': amountParam(minor),
        'currency': ccy,
        'reference': ref.isEmpty ? 'Transfer' : (ref.length > 35 ? ref.substring(0, 35) : ref),
      }));
      await refreshLedger();
      return (true, 'Posted as entry ${p.str('entry_id')} ($minor minor units).');
    } catch (e) {
      return (false, 'Refused: $e');
    }
  }

  Future<Outcome> openAccount(String name, String currency, String kind) async {
    final n = name.trim();
    if (n.isEmpty) return (false, 'Give the account a name.');
    try {
      final o = J(await _call('ledger.open_account',
          {'name': n.length > 63 ? n.substring(0, 63) : n, 'currency': currency, 'kind': kind}));
      await refreshLedger();
      await select(o.integer('account'));
      return (true, 'Opened $n.');
    } catch (e) {
      return (false, 'Refused: $e');
    }
  }

  Future<void> checkBooks() async {
    booksOK = J(await _safe('ledger.check')).boolean('ok');
    notifyListeners();
  }

  // ---------- P2P Mesh and Storefront ----------

  Future<bool> net(String command) async {
    try {
      final r = J(await _call('net.command', {'command': command})).str('result');
      final ok = r != 'refused' && r != 'could not start';
      netLog.insert(0, LogLine(command, r, ok));
      final s = J(await _safe('swarm.state'));
      if (!s.isNull) state = s;
      notifyListeners();
      return ok;
    } catch (e) {
      netLog.insert(0, LogLine(command, '$e', false));
      notifyListeners();
      return false;
    }
  }

  // ---------- Settings ----------

  Future<String> checkUpdate() async {
    try {
      return J(await _call('update.check', null, const Duration(seconds: 60))).str('status');
    } catch (e) {
      return '$e';
    }
  }

  Future<void> markAllRead() async {
    await _safe('notes.mark_read');
    await refresh();
  }

  Future<Outcome> enableCurrency(String code) async {
    final c = code.trim().toUpperCase();
    if (!RegExp(r'^[A-Z0-9]{3,8}$').hasMatch(c)) return (false, 'Use a code like GBP.');
    try {
      final e = J(await _call('currency.enable', {'code': c})).str('enabled');
      if (!currenciesEnabled.contains(e)) currenciesEnabled.add(e);
      notifyListeners();
      return (true, '$e enabled.');
    } catch (e) {
      return (false, 'Refused: $e');
    }
  }

  String formatBalance(String minor) => formatMinor(minor, selected?.str('currency') ?? '');
}
