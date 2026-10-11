// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Fail-closed: a halted context can never be used again. With the default
// policy the SDK throws ZxvFatalError (outside the ZxvError hierarchy);
// with fatalPolicy 'abort' the process ends at once.
//
//   node dist/examples/abort-on-fatal.js [abort]

import { Ledger, Zxv, ZxvError, ZxvFatalError } from '../src/index.js';

const zxv = await Zxv.load();
const ctx = zxv.createContext({ fatalPolicy: process.argv[2] === 'abort' ? 'abort' : 'throw' });
const cap = ctx.rootCapability();
ctx.enableCurrency(cap, 'GHS');
const ledger = Ledger.of(cap);
const acct = ledger.openAccount('treasury', 'GHS');

ctx.halt('operator kill switch');
try {
  ledger.balance(acct);
} catch (e) {
  if (e instanceof ZxvError) console.log('never reached: FATAL is not an ordinary error');
  if (e instanceof ZxvFatalError) console.log(`fatal: ${e.statusName}; the context only accepts close()`);
}
ctx.close();
