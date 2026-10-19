// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Settings: this phone's pairing, zxvd.version, update.check and
// swarm.state.update, notes.list / notes.mark_read, currency.enable and the
// Mac's machine budget (as macos/studio SettingsView.swift). Stopping zxvd
// and pairing phones stay on the Mac.
import 'package:flutter/material.dart';

import '../money.dart';
import '../store.dart';
import 'theme.dart';
import 'widgets.dart';

class SettingsView extends StatefulWidget {
  const SettingsView({super.key, required this.store});
  final AppStore store;

  @override
  State<SettingsView> createState() => _SettingsViewState();
}

class _SettingsViewState extends State<SettingsView> {
  String? _updateLine;
  bool _checking = false;
  final _code = TextEditingController();
  Outcome? _codeLine;
  bool _confirmForget = false;
  bool _editAddress = false;
  final _host = TextEditingController();
  final _port = TextEditingController();

  @override
  void dispose() {
    _code.dispose();
    _host.dispose();
    _port.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    final u = s.state['update'];
    final p = s.pairing;
    return ListView(padding: const EdgeInsets.all(16), children: [
      const PageHeader(
          title: 'Settings',
          subtitle: 'This phone\'s pairing, updates, notifications, currencies, and your Mac\'s budget.'),
      Panel(title: 'This phone', children: [
        KV('Paired as', p?.name ?? '—'),
        KV('Mac address', p == null ? '—' : '${p.host}:${p.port}'),
        KV('Link', 'zxv-phone/1 · X25519 + ChaCha20-Poly1305'),
        const FormLine(true,
            'Pairing and stopping zxvd happen on the Mac. To remove this phone there, revoke it in ZXV Studio.'),
        const SizedBox(height: 8),
        if (_editAddress) ...[
          Row(children: [
            Expanded(flex: 3, child: TextField(controller: _host, decoration: const InputDecoration(labelText: 'Address'))),
            const SizedBox(width: 8),
            Expanded(
                child: TextField(
                    controller: _port,
                    keyboardType: TextInputType.number,
                    decoration: const InputDecoration(labelText: 'Port'))),
          ]),
          const SizedBox(height: 8),
          Wrap(spacing: 8, children: [
            FilledButton(
              onPressed: () {
                final port = int.tryParse(_port.text.trim());
                if (_host.text.trim().isEmpty || port == null || port < 1 || port > 65535) return;
                setState(() => _editAddress = false);
                s.changeAddress(_host.text.trim(), port);
              },
              child: const Text('Save address'),
            ),
            TextButton(onPressed: () => setState(() => _editAddress = false), child: const Text('Cancel')),
          ]),
        ] else
          Wrap(spacing: 8, runSpacing: 8, children: [
            OutlinedButton(
              onPressed: p == null
                  ? null
                  : () => setState(() {
                        _host.text = p.host;
                        _port.text = '${p.port}';
                        _editAddress = true;
                      }),
              child: const Text('Mac moved? Change address'),
            ),
            if (!_confirmForget)
              TextButton(onPressed: () => setState(() => _confirmForget = true), child: const Text('Forget this Mac'))
            else ...[
              TextButton(onPressed: () => setState(() => _confirmForget = false), child: const Text('Cancel')),
              FilledButton(
                style: FilledButton.styleFrom(backgroundColor: Zc.warn),
                onPressed: s.forget,
                child: const Text('Forget pairing'),
              ),
            ],
          ]),
      ]),
      Panel(title: 'Updates', children: [
        KV('App', s.version.str('app', '—')),
        KV('Protocol', s.version.str('protocol', '—')),
        KV('libzxv ABI', s.version.str('libzxv', '—')),
        KV('Automatic checks', u.boolean('auto') ? 'on while online' : 'off'),
        KV('Checks run', grouped(u.integer('checks'))),
        FormLine(true, _updateLine ?? (u.str('status').isEmpty ? 'No check has run yet.' : u.str('status'))),
        if (u.str('error').isNotEmpty) FormLine(false, u.str('error')),
        const SizedBox(height: 8),
        OutlinedButton(
          onPressed: _checking
              ? null
              : () async {
                  setState(() => _checking = true);
                  final r = await s.checkUpdate();
                  if (!mounted) return;
                  setState(() {
                    _updateLine = r;
                    _checking = false;
                  });
                },
          child: Text(_checking ? 'Checking' : 'Check now'),
        ),
      ]),
      Panel(title: 'Notifications · ${s.unread} unread', children: [
        if (s.notes.isEmpty) const Text('No notifications.', style: TextStyle(color: Zc.muted)),
        for (final n in s.notes)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 5),
            child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Container(
                  margin: const EdgeInsets.only(top: 6, right: 10),
                  width: 8,
                  height: 8,
                  decoration: BoxDecoration(shape: BoxShape.circle, color: n.boolean('read') ? Zc.border : Zc.gold)),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text(n.integer('count') > 1 ? '${n.str('title')} (${n.integer('count')})' : n.str('title'),
                      style: const TextStyle(fontWeight: FontWeight.w500)),
                  if (n.str('body').isNotEmpty) Text(n.str('body'), style: const TextStyle(fontSize: 13)),
                  Text('${n.str('kind')} · from ${n.str('source')}',
                      style: const TextStyle(fontSize: 12, color: Zc.muted)),
                ]),
              ),
            ]),
          ),
        Align(
          alignment: Alignment.centerRight,
          child: TextButton(onPressed: s.unread == 0 ? null : s.markAllRead, child: const Text('Mark all read')),
        ),
      ]),
      Panel(title: 'Currencies in your books', children: [
        Text(s.currenciesEnabled.isEmpty ? 'None enabled yet.' : s.currenciesEnabled.join('  '),
            style: s.currenciesEnabled.isEmpty ? const TextStyle(color: Zc.muted) : Zc.mono),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(
              child: TextField(
                  controller: _code,
                  textCapitalization: TextCapitalization.characters,
                  decoration: const InputDecoration(hintText: 'ISO 4217 code, like GBP'))),
          const SizedBox(width: 8),
          FilledButton(
            onPressed: () async {
              final r = await s.enableCurrency(_code.text);
              if (!mounted) return;
              setState(() {
                _codeLine = r;
                if (r.$1) _code.clear();
              });
            },
            child: const Text('Enable'),
          ),
        ]),
        FormLine(_codeLine?.$1 ?? true, _codeLine?.$2 ?? 'Enabled currencies can hold accounts in the Sicily Ledger.'),
      ]),
      Panel(title: 'Your Mac\'s budget', children: [
        KV('Cores',
            s.state.isNull ? '—' : '${s.state.integer('cores')} total, ${_milli(s.state.integer('cores_milli'))} granted'),
        KV('Memory',
            s.state.isNull ? '—' : '${grouped(s.state.integer('mem_total_mb'))} MB, ${grouped(s.state.integer('mem_mb'))} granted'),
        KV('Swarm levels', s.state.isNull ? '—' : '${s.state.integer('levels')}'),
        KV('Tokens per cycle', grouped(s.state.integer('tokens_per_cycle'))),
      ]),
      if (!s.has('jurisdiction.get'))
        const NotServed(
            what: 'Jurisdiction Bridge',
            method: 'jurisdiction.get',
            detail:
                'Closed clearing is the default; an open batch-auction market is a per-deployment switch. The accounting standard for reports will be chosen here.'),
    ]);
  }

  static String _milli(int m) => '${m ~/ 1000}.${(m % 1000).toString().padLeft(3, '0').substring(0, 2)}';
}
