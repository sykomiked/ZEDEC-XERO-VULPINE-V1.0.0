// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/src/net_command.dart';

void main() {
  test('commands match what zxv_host.c parses', () {
    expect(modeCommand(NetMode.lan), 'lan');
    expect(peerCommand('192.168.1.20:8723'), 'peer 192.168.1.20:8723');
    expect(shareCommand(0, 0), 'share 0 0');
    expect(wantCommand(NetResource.storage, 4, 100), 'want storage 4 100');
    final id = 'AB' * 32;
    expect(sellCommand(NetResource.compute, 40, 2400, id), 'sell compute 40 2400 ${id.toLowerCase()}');
  });

  test('malformed commands are never built', () {
    for (final a in ['1.2.3.4', '256.1.1.1:80', '1.2.3.4:0', '1.2.3.4:65536', 'host:80', '1.2.3.4:8a']) {
      expect(() => peerCommand(a), throwsA(isA<NetCommandError>()), reason: a);
    }
    expect(() => wantCommand(NetResource.compute, 0, 1), throwsA(isA<NetCommandError>()));
    expect(() => sellCommand(NetResource.compute, 1, 1, 'abc'), throwsA(isA<NetCommandError>()));
    expect(() => shareCommand(-1, 0), throwsA(isA<NetCommandError>()));
  });
}
