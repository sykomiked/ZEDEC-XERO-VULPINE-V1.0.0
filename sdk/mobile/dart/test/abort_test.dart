// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:io';

import 'package:test/test.dart';

void main() {
  test('FatalPolicy.abort ends the process with 70', () async {
    final r = await Process.run(Platform.resolvedExecutable, ['run', 'test/abort_probe.dart']);
    final out = '${r.stdout}${r.stderr}';
    expect(r.exitCode, 70, reason: out);
    expect(out, contains('ZXV FATAL (FatalPolicy.abort)'));
    expect(out, isNot(contains('CAUGHT')));
    expect(out, isNot(contains('FINALLY')));
    expect(out, isNot(contains('SURVIVED')));
  });
}
