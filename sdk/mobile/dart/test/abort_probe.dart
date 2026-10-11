// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Run by abort_test.dart in a child process: FatalPolicy.abort must end the
// process with 70, past every catch and finally.
import 'package:zxv/zxv.dart';

void main() {
  try {
    final ctx = ZxvContext.create(policy: FatalPolicy.abort);
    final root = ctx.rootCapability(Permission.all);
    ctx.halt('abort probe');
    try {
      root.info();
    } catch (_) {
      print('CAUGHT');
    }
  } finally {
    print('FINALLY');
  }
  print('SURVIVED');
}
