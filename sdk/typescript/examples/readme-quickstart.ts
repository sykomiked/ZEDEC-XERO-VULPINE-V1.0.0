// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The README's 10-line quickstart, kept compiling and running.
//   node dist/examples/readme-quickstart.js

import { Zxv, Ledger, AccountKind, Cap, Rational } from '../src/index.js';

const zxv = await Zxv.load();                       // native in Node, wasm in a browser
using ctx = zxv.createContext();
const root = ctx.rootCapability();                  // hand out root.attenuate(...) to the rest
ctx.enableCurrency(root, 'NGN');
const ledger = Ledger.of(root);
const bank = ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
const ada = ledger.openAccount('ada', 'NGN');
ledger.post(bank, ada, 250_050n, 'NGN', 'E2E-1');   // exact kobo, as bigint
console.log(Ledger.of(root.attenuate(Cap.LEDGER_READ)).balance(ada).equity, Rational.parse('0.1').add('0.2').toString());
