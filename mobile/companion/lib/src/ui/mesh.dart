// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// P2P Mesh: net.command (off / lan / online / peer / share) and
// swarm.state.net (as macos/studio MeshView.swift). These are the Mac's
// network settings, changed from the phone.
import 'package:flutter/material.dart';

import '../money.dart';
import '../net_command.dart';
import '../store.dart';
import 'theme.dart';
import 'widgets.dart';

class MeshView extends StatefulWidget {
  const MeshView({super.key, required this.store});
  final AppStore store;

  @override
  State<MeshView> createState() => _MeshViewState();
}

class _MeshViewState extends State<MeshView> {
  final _peer = TextEditingController();
  final _compute = TextEditingController();
  final _storage = TextEditingController();
  String? _formError;

  @override
  void dispose() {
    _peer.dispose();
    _compute.dispose();
    _storage.dispose();
    super.dispose();
  }

  Future<void> _send(String Function() build) async {
    final String line;
    try {
      line = build();
    } on NetCommandError catch (e) {
      setState(() => _formError = e.message);
      return;
    }
    setState(() => _formError = null);
    await widget.store.net(line);
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    final n = s.state['net'];
    final mode = NetMode.values.firstWhere((m) => m.name == n.str('mode', 'off'), orElse: () => NetMode.off);
    return ListView(padding: const EdgeInsets.all(16), children: [
      const PageHeader(
          title: 'P2P Mesh',
          subtitle: 'Your Mac\'s Vinea peer network. It is off by default, and nothing leaves the Mac until you turn it on.'),
      SegmentedButton<NetMode>(
        segments: const [
          ButtonSegment(value: NetMode.off, label: Text('Off')),
          ButtonSegment(value: NetMode.lan, label: Text('LAN')),
          ButtonSegment(value: NetMode.online, label: Text('Online')),
        ],
        selected: {mode},
        onSelectionChanged: (v) => _send(() => modeCommand(v.first)),
      ),
      const SizedBox(height: 10),
      Text(switch (mode) {
        NetMode.off => 'Off: no socket is open. The swarm runs on the Mac only.',
        NetMode.lan => 'LAN: listening on your local network only.',
        NetMode.online => 'Online: reachable by peers you add. Signed updates are checked while online.',
      }, style: const TextStyle(color: Zc.muted)),
      if (n.str('error').isNotEmpty)
        Padding(
            padding: const EdgeInsets.only(top: 6),
            child: Text('Network error: ${n.str('error')}', style: const TextStyle(color: Zc.warn))),
      const SizedBox(height: 14),
      Panel(title: 'This node', trailing: n.boolean('bound') ? 'Bound' : 'Not running', children: [
        KV('Node ID', n.str('node_id').isEmpty ? '—' : n.str('node_id')),
        KV('Listening on', n.boolean('bound') ? '${n.str('bind')}:${n.integer('port')}' : '—'),
        const KV('Handshake', 'ML-KEM-1024 + ML-DSA-87'),
        const SizedBox(height: 6),
        const Text('Full NodeID', style: TextStyle(fontSize: 12, color: Zc.muted)),
        SelectableText(n.str('id').isEmpty ? 'The NodeID appears when the network is on.' : n.str('id'),
            style: Zc.mono.copyWith(fontSize: 12)),
      ]),
      Panel(title: 'Traffic', children: [
        StatGrid([
          Stat(grouped(n.integer('peers')), 'Peers'),
          Stat(grouped(n.integer('in')), 'Datagrams in'),
          Stat(grouped(n.integer('out')), 'Datagrams out'),
          Stat(grouped(n.integer('refused')), 'Refused'),
          Stat(grouped(n.integer('policy_drops')), 'Policy drops'),
          Stat(grouped(n.integer('send_errors')), 'Send errors'),
        ]),
        const SizedBox(height: 10),
        Text(
            'Imports ${n.boolean('conserved', true) ? 'conserved' : 'NOT conserved'} · ${grouped(n.integer('imported'))} units imported',
            style: TextStyle(color: n.boolean('conserved', true) ? Zc.muted : Zc.warn)),
      ]),
      Panel(title: 'Add a peer', children: [
        TextField(controller: _peer, decoration: InputDecoration(hintText: '192.168.1.20:$defaultNetPort')),
        const SizedBox(height: 8),
        FilledButton(
          onPressed: mode == NetMode.off ? null : () => _send(() => peerCommand(_peer.text.trim())),
          child: const Text('Add peer'),
        ),
        if (mode == NetMode.off)
          const FormLine(true, 'Turn on LAN or Online first.'),
        const Divider(height: 28),
        const Text('SHARE WITH THE MESH',
            style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, letterSpacing: 1, color: Zc.muted)),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(
              child: TextField(
                  controller: _compute,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(hintText: 'Compute units'))),
          const SizedBox(width: 8),
          Expanded(
              child: TextField(
                  controller: _storage,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(hintText: 'Storage units'))),
        ]),
        const SizedBox(height: 8),
        OutlinedButton(
          onPressed: () => _send(() => shareCommand(
              int.tryParse(_compute.text.trim()) ?? 0, int.tryParse(_storage.text.trim()) ?? 0)),
          child: const Text('Update sharing'),
        ),
        FormLine(true,
            'Now sharing ${grouped(n.integer('share_compute'))} compute and ${grouped(n.integer('share_storage'))} storage units. 0 and 0 stops sharing.'),
        if (_formError != null) FormLine(false, _formError!),
      ]),
      Panel(title: 'Network log', children: [
        if (s.netLog.isEmpty)
          const Text('Commands you send to zxvd appear here.', style: TextStyle(color: Zc.muted)),
        for (final l in s.netLog.take(8))
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 3),
            child: Text.rich(TextSpan(children: [
              TextSpan(text: '${l.command}  ', style: const TextStyle(color: Zc.gold)),
              TextSpan(text: l.result, style: TextStyle(color: l.ok ? Zc.text : Zc.warn)),
            ])),
          ),
      ]),
      if (!s.has('net.peers'))
        const NotServed(
            what: 'Peers',
            method: 'net.peers',
            detail: 'A row per peer with its NodeID, address, last contact and traffic. zxvd reports only the peer count today.'),
    ]);
  }
}
