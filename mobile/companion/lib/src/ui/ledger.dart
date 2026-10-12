// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Sicily Ledger & Patron Deck: ledger.accounts / balance / entries / post /
// open_account / check, swarm.earnings, and swarm.state.quest for the deck
// (as macos/studio LedgerView.swift).
import 'package:flutter/material.dart';

import '../json.dart';
import '../money.dart';
import '../store.dart';
import 'theme.dart';
import 'zxv_bar.dart';
import 'widgets.dart';

class LedgerView extends StatefulWidget {
  const LedgerView({super.key, required this.store});
  final AppStore store;

  @override
  State<LedgerView> createState() => _LedgerViewState();
}

class _LedgerViewState extends State<LedgerView> {
  bool _showOpen = false;
  final _newName = TextEditingController();
  String _newCurrency = '';
  String _newKind = 'holder';
  Outcome? _openLine;
  int? _to;
  final _amount = TextEditingController();
  final _reference = TextEditingController();
  Outcome? _postLine;
  bool _posting = false;

  @override
  void dispose() {
    _newName.dispose();
    _amount.dispose();
    _reference.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    return RefreshIndicator(
      onRefresh: s.refreshLedger,
      child: ListView(padding: const EdgeInsets.all(16), children: [
        PageHeader(
          title: 'Sicily Ledger',
          subtitle: 'Your books in whole minor units. Every transfer balances; fractions are refused, never rounded.',
          trailing: IconButton(
              tooltip: 'Check books', icon: const Icon(Icons.fact_check_outlined), onPressed: s.checkBooks),
        ),
        if (s.booksOK != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: Text(s.booksOK! ? 'Books balanced' : 'Books check failed',
                style: TextStyle(color: s.booksOK! ? Zc.good : Zc.warn)),
          ),
        _earnings(s.earnings),
        if (s.accounts.isEmpty)
          const Panel(title: 'Accounts', children: [
            Text('No accounts yet. Enable a currency in Settings, then open an issuer and a holder account.',
                style: TextStyle(color: Zc.muted)),
          ])
        else ...[
          _accountList(s),
          _rails(s),
          _journal(s),
          _transfer(s),
        ],
        _openAccount(s),
        _deck(s),
      ]),
    );
  }

  Widget _earnings(J e) => e.isNull
      ? const SizedBox.shrink()
      : Panel(title: 'Swarm earnings', trailing: e.str('currency'), children: [
          Text(formatMinor(e.str('balance_minor', '0'), e.str('currency')),
              style: Zc.mono.copyWith(fontSize: 22, color: Zc.text)),
          KV('Rate per swarm credit', e.str('rate_minor')),
          KV('Postings', e.str('postings')),
          KV('Cycles', e.str('cycles')),
        ]);

  Widget _accountList(AppStore s) => Panel(title: 'Accounts', children: [
        for (final a in s.accounts)
          InkWell(
            borderRadius: BorderRadius.circular(10),
            onTap: () {
              s.select(a.integer('account'));
              setState(() {
                _to = null;
                _postLine = null;
              });
            },
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
              decoration: BoxDecoration(
                color: a.integer('account') == s.selectedAccount ? Zc.border : Colors.transparent,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Row(children: [
                Expanded(
                  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Text(a.str('name')),
                    Text('${a.str('kind')} · ${a.str('currency')}',
                        style: const TextStyle(fontSize: 12, color: Zc.muted)),
                  ]),
                ),
              ]),
            ),
          ),
      ]);

  Widget _rails(AppStore s) {
    final b = s.balance;
    Widget tile(String title, String rail, String key) => Panel(title: title, children: [
          Text(s.formatBalance(b.str(key, '?')), style: Zc.mono.copyWith(fontSize: 20, color: Zc.text)),
          Text('$rail · ${b.str(key, '?')} minor', style: Zc.mono.copyWith(fontSize: 11, color: Zc.muted)),
        ]);
    return Column(children: [
      tile('Assets · Neo-Roman', 'debit rail 555', 'debit_minor'),
      tile('Liabilities · New California', 'credit rail 777', 'credit_minor'),
      tile('Equity · Sicilian Crown', 'equity rail 888', 'equity_minor'),
    ]);
  }

  Widget _journal(AppStore s) {
    final ccy = s.selected?.str('currency') ?? '';
    return Panel(title: 'Journal · ${s.selected?.str('name') ?? ''}', children: [
      if (s.entries.isEmpty) const Text('No entries yet.', style: TextStyle(color: Zc.muted)),
      for (final e in s.entries.reversed)
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 5),
          child: Row(children: [
            Expanded(
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text(e.str('reference')),
                Text('#${e.str('entry_id')} · ${s.nameOf(e.integer('counterparty'))}',
                    style: const TextStyle(fontSize: 12, color: Zc.muted)),
              ]),
            ),
            Text(
              '${(BigInt.tryParse(e.str('signed_minor')) ?? BigInt.zero) > BigInt.zero ? '+' : ''}${formatMinor(e.str('signed_minor'), ccy)}',
              style: Zc.mono.copyWith(
                  fontSize: 13,
                  color: e.str('signed_minor').startsWith('-') ? Zc.warn : Zc.good),
            ),
          ]),
        ),
    ]);
  }

  Widget _transfer(AppStore s) {
    final from = s.selected;
    final targets = s.accounts
        .where((a) =>
            a.integer('account') != from?.integer('account') && a.str('currency') == from?.str('currency'))
        .toList();
    final to = targets.any((a) => a.integer('account') == _to) ? _to : (targets.isEmpty ? null : targets.first.integer('account'));
    return Panel(title: 'New transfer', children: [
      Labeled('From', Text(from?.str('name') ?? '')),
      Labeled(
        'To',
        targets.isEmpty
            ? Text('Open a second ${from?.str('currency') ?? ''} account to transfer to.',
                style: const TextStyle(color: Zc.muted))
            : DropdownButton<int>(
                isExpanded: true,
                value: to,
                items: [
                  for (final t in targets) DropdownMenuItem(value: t.integer('account'), child: Text(t.str('name')))
                ],
                onChanged: (v) => setState(() => _to = v),
              ),
      ),
      Labeled(
          'Amount (${from?.str('currency') ?? ''})',
          TextField(
              controller: _amount,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              decoration: const InputDecoration(hintText: '125.00'))),
      Labeled('Reference', TextField(controller: _reference, decoration: const InputDecoration(hintText: 'Invoice 0042'))),
      FilledButton(
        onPressed: _posting || from == null || to == null
            ? null
            : () async {
                setState(() => _posting = true);
                final r = await s.post(to, _amount.text, _reference.text);
                if (!mounted) return;
                setState(() {
                  _posting = false;
                  _postLine = r;
                  if (r.$1) {
                    _amount.clear();
                    _reference.clear();
                  }
                });
              },
        child: const Text('Post transfer'),
      ),
      FormLine(_postLine?.$1 ?? true, _postLine?.$2 ?? 'Amounts go to zxvd as whole minor units.'),
    ]);
  }

  Widget _openAccount(AppStore s) {
    final currencies = s.currenciesEnabled.isEmpty ? ['USD'] : s.currenciesEnabled;
    final ccy = currencies.contains(_newCurrency) ? _newCurrency : currencies.first;
    return Panel(title: 'Open account', children: [
      if (!_showOpen)
        OutlinedButton(onPressed: () => setState(() => _showOpen = true), child: const Text('Open an account'))
      else ...[
        Labeled('Name', TextField(controller: _newName, decoration: const InputDecoration(hintText: 'Travel fund'))),
        Row(children: [
          Expanded(
            child: Labeled(
              'Currency',
              DropdownButton<String>(
                isExpanded: true,
                value: ccy,
                items: [for (final c in currencies) DropdownMenuItem(value: c, child: Text(c))],
                onChanged: (v) => setState(() => _newCurrency = v ?? ccy),
              ),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Labeled(
              'Kind',
              DropdownButton<String>(
                isExpanded: true,
                value: _newKind,
                items: const [
                  DropdownMenuItem(value: 'holder', child: Text('holder')),
                  DropdownMenuItem(value: 'issuer', child: Text('issuer')),
                ],
                onChanged: (v) => setState(() => _newKind = v ?? 'holder'),
              ),
            ),
          ),
        ]),
        FilledButton(
          onPressed: () async {
            final r = await s.openAccount(_newName.text, ccy, _newKind);
            if (!mounted) return;
            setState(() {
              _openLine = r;
              if (r.$1) {
                _newName.clear();
                _showOpen = false;
              }
            });
          },
          child: const Text('Open'),
        ),
      ],
      if (_openLine != null) FormLine(_openLine!.$1, _openLine!.$2),
    ]);
  }

  Widget _deck(AppStore s) {
    final q = s.state['quest'];
    return Column(children: [
      Panel(
        title: 'Patron Deck',
        trailing: 'Contributed ${grouped(q.integer('contributed'))} units',
        children: [
          const Text('Levels and points only, no ranks or streaks.', style: TextStyle(fontSize: 12, color: Zc.muted)),
          const SizedBox(height: 10),
          for (final k in q.list('skills')) _SkillCard(k),
        ],
      ),
      if (!s.has('player.card'))
        const NotServed(
            what: 'Player Card',
            method: 'player.card',
            detail: 'Tiers run Copper/Obsidian to Curzi/Admas and are earned from credibility, bond and uptime.'),
    ]);
  }
}

class _SkillCard extends StatelessWidget {
  const _SkillCard(this.k);
  final J k;

  /// Points needed to reach each level (kernel/src/quest/quest.c thresholds).
  static const thresholds = [0, 8, 21, 55, 144, 377, 987, 2584];

  @override
  Widget build(BuildContext context) {
    final lv = k.integer('level').clamp(0, thresholds.length - 1);
    final pts = k.integer('points');
    final floor = thresholds[lv];
    final top = lv + 1 < thresholds.length ? thresholds[lv + 1] : floor;
    final frac = top == floor ? 1.0 : ((pts - floor) / (top - floor)).clamp(0.0, 1.0);
    final name = k.str('skill');
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(12),
      decoration:
          BoxDecoration(color: Zc.inset, borderRadius: BorderRadius.circular(12), border: Border.all(color: Zc.border)),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Expanded(child: Text(name.isEmpty ? name : name[0].toUpperCase() + name.substring(1))),
          Text('Level ${k.integer('level')}', style: const TextStyle(fontSize: 12, color: Zc.gold)),
        ]),
        const SizedBox(height: 6),
        ZxvBar(value: frac),
        const SizedBox(height: 4),
        Text(top == floor ? '$pts pts · top level' : '$pts pts · ${top - pts} to level ${lv + 1}',
            style: Zc.mono.copyWith(fontSize: 11, color: Zc.muted)),
      ]),
    );
  }
}
