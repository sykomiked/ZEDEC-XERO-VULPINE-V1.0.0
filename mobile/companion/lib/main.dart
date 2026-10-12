// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZXV Companion: the owner's zxvd node on a phone. The same five views as the
// Mac app (macos/studio), over zxvd's encrypted phone link.
import 'package:flutter/material.dart';

import 'src/pairing.dart';
import 'src/store.dart';
import 'src/ui/ledger.dart';
import 'src/ui/mesh.dart';
import 'src/ui/pair.dart';
import 'src/ui/settings.dart';
import 'src/ui/storefront.dart';
import 'src/ui/studio.dart';
import 'src/ui/theme.dart';
import 'src/ui/zxv_bar.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  final store = AppStore(pairings: SecurePairingStore())..start();
  runApp(CompanionApp(store: store));
}

class CompanionApp extends StatelessWidget {
  const CompanionApp({super.key, required this.store, this.allowScan = true});
  final AppStore store;
  final bool allowScan;

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'ZXV Companion',
        debugShowCheckedModeBanner: false,
        theme: zxvTheme(),
        home: ListenableBuilder(
          listenable: store,
          builder: (context, _) =>
              store.pairing == null ? PairView(store: store, allowScan: allowScan) : Home(store: store),
        ),
      );
}

class Home extends StatefulWidget {
  const Home({super.key, required this.store});
  final AppStore store;

  @override
  State<Home> createState() => _HomeState();
}

class _HomeState extends State<Home> with WidgetsBindingObserver {
  int _tab = 0;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState s) =>
      widget.store.setForeground(s == AppLifecycleState.resumed);

  @override
  Widget build(BuildContext context) {
    final s = widget.store;
    final views = [
      StudioView(store: s),
      LedgerView(store: s),
      MeshView(store: s),
      StorefrontView(store: s),
      SettingsView(store: s),
    ];
    return Scaffold(
      body: SafeArea(
        child: Column(children: [
          _StatusBar(store: s),
          Expanded(child: s.link == LinkState.connected ? views[_tab] : _NotConnected(store: s)),
        ]),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: [
          const NavigationDestination(icon: Icon(Icons.chat_bubble_outline), label: 'Studio'),
          const NavigationDestination(icon: Icon(Icons.account_balance_outlined), label: 'Ledger'),
          const NavigationDestination(icon: Icon(Icons.hub_outlined), label: 'Mesh'),
          const NavigationDestination(icon: Icon(Icons.storefront_outlined), label: 'Store'),
          NavigationDestination(
              icon: Badge(isLabelVisible: s.unread > 0, label: Text('${s.unread}'), child: const Icon(Icons.tune)),
              label: 'Settings'),
        ],
      ),
    );
  }
}

class _StatusBar extends StatelessWidget {
  const _StatusBar({required this.store});
  final AppStore store;

  @override
  Widget build(BuildContext context) {
    final ok = store.link == LinkState.connected;
    return Container(
      color: Zc.sidebar,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(children: [
        Container(
            width: 8, height: 8, decoration: BoxDecoration(shape: BoxShape.circle, color: ok ? Zc.good : Zc.warn)),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            switch (store.link) {
              LinkState.connected => 'Connected to ${store.version.str('app', 'zxvd')} on your Mac',
              LinkState.connecting => 'Connecting to your Mac',
              _ => 'Not connected',
            },
            style: const TextStyle(fontSize: 13),
            overflow: TextOverflow.ellipsis,
          ),
        ),
        if (store.booksOK != null)
          Text(store.booksOK! ? 'Books balanced' : 'Books check failed',
              style: TextStyle(fontSize: 12, color: store.booksOK! ? Zc.muted : Zc.warn)),
      ]),
    );
  }
}

class _NotConnected extends StatelessWidget {
  const _NotConnected({required this.store});
  final AppStore store;

  @override
  Widget build(BuildContext context) => Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            const Icon(Icons.link_off, size: 40, color: Zc.muted),
            const SizedBox(height: 14),
            Text(store.link == LinkState.connecting ? 'Connecting' : 'Your Mac is not reachable',
                style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
            const SizedBox(height: 8),
            Text(
              store.link == LinkState.connecting
                  ? '${store.pairing?.host}:${store.pairing?.port}'
                  : 'Check that the Mac is awake, ZXV Studio is open with phones allowed, and both are on the same network.\n\n${store.downReason}',
              textAlign: TextAlign.center,
              style: const TextStyle(color: Zc.muted),
            ),
            const SizedBox(height: 16),
            if (store.link == LinkState.connecting) const SizedBox(width: 220, child: ZxvBar()),
            if (store.link == LinkState.down)
              FilledButton(onPressed: store.connect, child: const Text('Try again')),
          ]),
        ),
      );
}
