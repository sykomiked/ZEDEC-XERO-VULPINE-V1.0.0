// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The text commands net.command takes (zxv_host.c), built and checked here
// so the app never sends a malformed line (as macos/studio NetCommand.swift):
// off, lan, online, peer IP:PORT, share C S, want RES UNITS MAX,
// sell RES UNITS PRICE NODEID.

const defaultNetPort = 8723;

enum NetMode { off, lan, online }

enum NetResource { compute, storage }

class NetCommandError implements Exception {
  NetCommandError(this.message);
  final String message;
  @override
  String toString() => message;
}

String modeCommand(NetMode m) => m.name;

String peerCommand(String addr) {
  if (!isIPv4Port(addr)) throw NetCommandError('Use an address like 192.168.1.20:$defaultNetPort.');
  return 'peer $addr';
}

String shareCommand(int compute, int storage) {
  if (compute < 0 || storage < 0) throw NetCommandError('Units cannot be negative.');
  return 'share $compute $storage';
}

String wantCommand(NetResource r, int units, int maxPrice) {
  if (units <= 0 || maxPrice <= 0) throw NetCommandError('Units and price must be above zero.');
  return 'want ${r.name} $units $maxPrice';
}

String sellCommand(NetResource r, int units, int price, String nodeId) {
  if (units <= 0 || price <= 0) throw NetCommandError('Units and price must be above zero.');
  if (!isNodeId(nodeId)) throw NetCommandError("The buyer's NodeID is 64 hex digits.");
  return 'sell ${r.name} $units $price ${nodeId.toLowerCase()}';
}

/// "192.168.1.20:8723": four 0-255 octets and a 1-65535 port.
bool isIPv4Port(String s) {
  final m = RegExp(r'^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}):(\d{1,5})$').firstMatch(s);
  if (m == null) return false;
  for (var i = 1; i <= 4; i++) {
    if (int.parse(m[i]!) > 255) return false;
  }
  final p = int.parse(m[5]!);
  return p >= 1 && p <= 65535;
}

bool isNodeId(String s) => RegExp(r'^[0-9a-fA-F]{64}$').hasMatch(s);
