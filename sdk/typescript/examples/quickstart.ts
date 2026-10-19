// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A tour of @zedec/zxv: exact money, capabilities, four-valued logic,
// post-quantum signatures, budgets and deterministic generation.
//
//   npm run build && node dist/examples/quickstart.js [native|wasm]

import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  AccountKind, Attestations, Budgets, Cap, Ledger, MlDsa87, Model, Rational, Truth, Ubh168Frame, Zxv,
} from '../src/index.js';

const backend = process.argv[2] === 'wasm' ? 'wasm' : process.argv[2] === 'native' ? 'native' : 'auto';
const zxv = await Zxv.load({ backend });
console.log(`libzxv ${zxv.version.major}.${zxv.version.minor}.${zxv.version.patch} via ${zxv.backend} (${zxv.origin})`);

// 1. Exact numbers: 0.1 + 0.2 is 0.3, and a float only enters on purpose.
console.log('0.1 + 0.2 =', Rational.parse('0.1').add(Rational.parse('0.2')).toString());
console.log('the double 0.1 is exactly', Rational.fromNumberExact(0.1).toFraction());

// 2. A context, a root capability, and a narrower one for read-only code.
using ctx = zxv.createContext();
const root = ctx.rootCapability();
const readOnly = root.attenuate(Cap.LEDGER_READ);

ctx.enableCurrency(root, 'NGN');
const ledger = Ledger.of(root);
const bank = ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
const ada = ledger.openAccount('ada', 'NGN');
ledger.post(bank, ada, 250_050n, 'NGN', 'E2E-1'); // 2,500.50 NGN in kobo
console.log('ada equity (kobo):', Ledger.of(readOnly).balance(ada).equity);
console.log('read-only cap may post?', readOnly.check(Cap.LEDGER_POST));

// 3. Contradictions are held, never collapsed.
using reg = Attestations.create(root);
const i = reg.attest('sensor-7', { attester: 'field-team' });
reg.evidence(i, false);
const t = reg.state(i).truth;
console.log(`sensor-7 is ${t}; actuate? ${t.toBoolean({ onBoth: false, onNeither: false })}`);
console.log('gate:', reg.gate() === Truth.TRUE ? 'clear' : 'blocked');

// 4. Post-quantum signatures; the secret key never touches the JS heap.
const { publicKey, secretKey } = MlDsa87.generate(root);
using sk = secretKey;
const sig = MlDsa87.sign(root, sk, 'agent message', { context: 'zxv' });
console.log('ML-DSA-87 verifies:', MlDsa87.verify(root, publicKey, 'agent message', sig, { context: 'zxv' }));

// 5. A UBH-168 frame in pure TypeScript.
const frame = Ubh168Frame.pack(Ubh168Frame.tag(3, 2, 1), [1, 2, 3], [4, 5]);
console.log('UBH-168:', frame.toHex(), 'resolves to wire truth', zxv.wire.resolve(frame));

// 6. Deterministic generation under a token budget.
const fixtures = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'conformance', 'fixtures');
using model = await Model.open(root, join(fixtures, 'tiny-qwen2.gguf'), join(fixtures, 'tiny-tokenizer.gguf'));
using budgets = Budgets.open(root);
using session = model.newSession({ nCtx: 128 });
session.bindBudget(budgets.create('tokens', 64));
const prompt = '<|im_start|>user\nHi there<|im_end|>\n<|im_start|>assistant\n';
const ids: number[] = [];
for await (const tok of session.stream(prompt, { maxTokens: 12, temperature: '0.7', topK: 40, seed: 42n })) ids.push(tok.id);
console.log('tokens:', ids.join(' '));
console.log('budget left:', session.budget?.status().remaining.toString(), 'of 64 tokens');
