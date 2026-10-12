// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// First run: pair with the Mac by scanning the QR code ZXV Studio shows
// (Settings, Pair a phone), or by pasting its link.
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:mobile_scanner/mobile_scanner.dart';

import '../store.dart';
import 'theme.dart';

class PairView extends StatefulWidget {
  const PairView({super.key, required this.store, this.allowScan = true});
  final AppStore store;
  final bool allowScan;

  @override
  State<PairView> createState() => _PairViewState();
}

class _PairViewState extends State<PairView> {
  final _link = TextEditingController();
  String? _error;
  bool _busy = false;

  @override
  void dispose() {
    _link.dispose();
    super.dispose();
  }

  Future<void> _pair(String text) async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _error = null;
    });
    final why = await widget.store.pair(text);
    if (!mounted) return;
    setState(() {
      _busy = false;
      _error = why;
    });
  }

  Future<void> _scan() async {
    final text = await Navigator.of(context).push<String>(MaterialPageRoute(builder: (_) => const _ScanPage()));
    if (text != null) {
      _link.text = text;
      await _pair(text);
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        body: SafeArea(
          child: ListView(padding: const EdgeInsets.all(24), children: [
            const SizedBox(height: 24),
            const Text('ZXV SWARM', style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, letterSpacing: 1)),
            const SizedBox(height: 8),
            const Text('Pair with your Mac',
                style: TextStyle(fontSize: 26, fontWeight: FontWeight.w600, color: Zc.text)),
            const SizedBox(height: 10),
            const Text(
                'On the Mac, open ZXV Studio, go to Settings, and choose Pair a phone. Then scan the code it shows. '
                'The phone and the Mac must be on the same network.',
                style: TextStyle(color: Zc.muted)),
            const SizedBox(height: 24),
            if (widget.allowScan)
              FilledButton.icon(
                onPressed: _busy ? null : _scan,
                icon: const Icon(Icons.qr_code_scanner),
                label: const Text('Scan the code'),
              ),
            const SizedBox(height: 20),
            const Text('Or paste the pairing link', style: TextStyle(fontSize: 12, color: Zc.muted)),
            const SizedBox(height: 6),
            TextField(
              key: const Key('link'),
              controller: _link,
              minLines: 2,
              maxLines: 4,
              autocorrect: false,
              decoration: InputDecoration(
                hintText: 'zxv-phone:1?h=...',
                suffixIcon: IconButton(
                  tooltip: 'Paste',
                  icon: const Icon(Icons.content_paste),
                  onPressed: () async {
                    final d = await Clipboard.getData(Clipboard.kTextPlain);
                    if (d?.text != null) _link.text = d!.text!;
                  },
                ),
              ),
            ),
            const SizedBox(height: 10),
            OutlinedButton(
              key: const Key('pair'),
              onPressed: _busy ? null : () => _pair(_link.text),
              child: Text(_busy ? 'Connecting' : 'Pair'),
            ),
            if (_error != null)
              Padding(padding: const EdgeInsets.only(top: 12), child: Text(_error!, style: const TextStyle(color: Zc.warn))),
            const SizedBox(height: 24),
            const Text(
                'The link holds this phone\'s key. It is kept in the phone\'s secure storage, and everything between '
                'the phone and the Mac is encrypted.',
                style: TextStyle(fontSize: 12, color: Zc.muted)),
          ]),
        ),
      );
}

class _ScanPage extends StatefulWidget {
  const _ScanPage();

  @override
  State<_ScanPage> createState() => _ScanPageState();
}

class _ScanPageState extends State<_ScanPage> {
  bool _done = false;

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('Scan the pairing code')),
        body: MobileScanner(
          onDetect: (c) {
            if (_done) return;
            for (final b in c.barcodes) {
              final v = b.rawValue;
              if (v != null && v.startsWith('zxv-phone:')) {
                _done = true;
                Navigator.of(context).pop(v);
                return;
              }
            }
          },
        ),
      );
}
