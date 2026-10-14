// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Storefront & Escrow: net.command sell / want, swarm.state.net's market
// counters and swarm.state's settlement spine (as macos/studio
// StorefrontView.swift).
import 'package:flutter/material.dart';

import '../money.dart';
import '../net_command.dart';
import '../store.dart';
import 'theme.dart';
import 'widgets.dart';

class StorefrontView extends StatefulWidget {
  const StorefrontView({super.key, required this.store});
  final AppStore store;

  @override
  State<StorefrontView> createState() => _StorefrontViewState();
}

class _StorefrontViewState extends State<StorefrontView> {
  bool _selling = true;
  NetResource _resource = NetResource.compute;
  final _units = TextEditingController();
  final _price = TextEditingController();
  final _node = TextEditingController();
  final List<String> _posted = [];

  @override
  void initState() {
    super.initState();
    for (final c in [_units, _price, _node]) {
      c.addListener(() => setState(() {}));
    }
  }

  @override
  void dispose() {
    _units.dispose();
    _price.dispose();
    _node.dispose();
    super.dispose();
  }

  (String?, String) _command() {
    final u = int.tryParse(_units.text.trim()) ?? 0;
    final p = int.tryParse(_price.text.trim()) ?? 0;
    try {
      final line = _selling ? sellCommand(_resource, u, p, _node.text.trim()) : wantCommand(_resource, u, p);
      return (line, line);
    } on NetCommandError catch (e) {
      return (null, e.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    final n = s.state['net'];
    final (line, preview) = _command();
    final bound = n.boolean('bound');
    return ListView(padding: const EdgeInsets.all(16), children: [
      const PageHeader(
          title: 'Storefront & Escrow',
          subtitle:
              'Sell spare compute and storage to peers, or post what you need. Every trade settles through your ledger with the 0.08889% assurance fee.'),
      Panel(title: 'Post an offer', children: [
        SegmentedButton<bool>(
          segments: const [ButtonSegment(value: true, label: Text('Sell')), ButtonSegment(value: false, label: Text('Want'))],
          selected: {_selling},
          onSelectionChanged: (v) => setState(() => _selling = v.first),
        ),
        const SizedBox(height: 12),
        Labeled(
          'Resource',
          DropdownButton<NetResource>(
            isExpanded: true,
            value: _resource,
            items: const [
              DropdownMenuItem(value: NetResource.compute, child: Text('Compute')),
              DropdownMenuItem(value: NetResource.storage, child: Text('Storage')),
            ],
            onChanged: (v) => setState(() => _resource = v ?? NetResource.compute),
          ),
        ),
        Labeled('Units',
            TextField(controller: _units, keyboardType: TextInputType.number, decoration: const InputDecoration(hintText: '40'))),
        Labeled(
            _selling ? 'Price, minor units' : 'Most you will pay, minor units',
            TextField(controller: _price, keyboardType: TextInputType.number, decoration: const InputDecoration(hintText: '2400'))),
        if (_selling)
          Labeled('Buyer NodeID (64 hex digits)',
              TextField(controller: _node, decoration: const InputDecoration(hintText: "from the buyer's P2P Mesh page"))),
        Text(preview, style: Zc.mono.copyWith(fontSize: 12, color: line == null ? Zc.muted : Zc.text)),
        const SizedBox(height: 8),
        FilledButton(
          onPressed: line == null || !bound
              ? null
              : () async {
                  if (await s.net(line)) {
                    setState(() {
                      _posted.insert(0, line);
                      _units.clear();
                      _price.clear();
                    });
                  }
                },
          child: Text(_selling ? 'Post sell offer' : 'Post want'),
        ),
        if (!bound) const FormLine(true, 'Turn the network on in P2P Mesh to trade.'),
        if (s.netLog.isNotEmpty && !s.netLog.first.ok) FormLine(false, s.netLog.first.result),
        if (_posted.isNotEmpty) ...[
          const SizedBox(height: 8),
          const Text('Posted from this phone', style: TextStyle(fontSize: 12, color: Zc.muted)),
          for (final p in _posted.take(5)) Text(p, style: Zc.mono.copyWith(fontSize: 12)),
        ],
      ]),
      Panel(title: 'Market', children: [
        KV('Open wants', grouped(n.integer('wants'))),
        KV('Trades', grouped(n.integer('trades'))),
        KV('Receipts refused', grouped(n.integer('receipts_refused'))),
        KV('Units imported', grouped(n.integer('imported'))),
        KV('Conserved', n.boolean('conserved', true) ? 'yes' : 'no',
            color: n.boolean('conserved', true) ? Zc.good : Zc.warn),
      ]),
      _settlement(s),
      if (!s.has('ledger.holds'))
        const NotServed(
            what: 'Escrow',
            method: 'ledger.holds',
            detail:
                'Each hold will show the amount held, the counterparty, what releases it, and whether it is held, released or in dispute.'),
      if (!s.has('net.offers'))
        const NotServed(
            what: 'Open offers',
            method: 'net.offers',
            detail: 'Your open sell offers and wants. zxvd reports only the number of wants today.'),
    ]);
  }

  Widget _settlement(AppStore store) {
    final s = store.state;
    final halt = s.integer('ledger_halt');
    return Panel(title: 'Settlement', trailing: halt != 0 ? 'Halted (reason $halt)' : 'Running, not halted', children: [
      StatGrid([
        Stat(grouped(s.integer('ledger_postings')), 'Ledger postings'),
        Stat(grouped(s.integer('ledger_synced')), 'Cycles synced'),
        Stat(grouped(s.integer('fees')), 'Fees'),
        Stat(grouped(s.integer('levied')), 'Levied'),
        Stat(grouped(s.integer('bounties_returned')), 'Bounties returned'),
        Stat('${grouped(s.integer('ledger_audited'))} / ${grouped(s.integer('ledger_issues'))}', 'Audited / issues'),
      ]),
      const SizedBox(height: 12),
      const Text('Where the 0.08889% fee goes', style: TextStyle(fontSize: 12, color: Zc.muted)),
      const SizedBox(height: 6),
      Semantics(
        label: 'Reserve floor 50%, V-Bill dividend 25%, node bounties 15%, regenerative 10%',
        child: ClipRRect(
          borderRadius: BorderRadius.circular(6),
          child: SizedBox(
            height: 12,
            child: Row(children: [
              Expanded(flex: 50, child: Container(color: Zc.floor)),
              Expanded(flex: 25, child: Container(color: Zc.gold)),
              Expanded(flex: 15, child: Container(color: Zc.good)),
              Expanded(flex: 10, child: Container(color: Zc.emotional)),
            ]),
          ),
        ),
      ),
      const SizedBox(height: 8),
      const Wrap(spacing: 14, runSpacing: 4, children: [
        Legend(Zc.floor, 'Reserve floor 50%'),
        Legend(Zc.gold, 'V-Bill dividend 25%'),
        Legend(Zc.good, 'Node bounties 15%'),
        Legend(Zc.emotional, 'Regenerative 10%'),
      ]),
    ]);
  }
}
