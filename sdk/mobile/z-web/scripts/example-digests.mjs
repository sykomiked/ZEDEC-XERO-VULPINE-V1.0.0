#!/usr/bin/env node
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Write example/digests.json: the URL (relative to example/index.html) and
// SHA3-256 of zxv.wasm and of the fixture model, tokenizer and prompt, so
// the example page can check every file before using it. Needs
// `npm run build` here, `npm run build:wasm` in sdk/typescript and
// `make -C sdk/libzxv fixtures`.

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { sha3_256Hex } from '../dist/src/sha3.js';

const pkg = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const sdk = resolve(pkg, '..', '..');
const vectors = JSON.parse(readFileSync(join(sdk, 'conformance', 'vectors.json'), 'utf8'));
const g = vectors.generate;

const entry = (rel) => ({ url: `../../../${rel}`, sha3_256: sha3_256Hex(new Uint8Array(readFileSync(join(sdk, rel)))) });
const out = {
  wasm: entry('typescript/wasm/zxv.wasm'),
  model: entry(`conformance/fixtures/${g.model}`),
  tokenizer: entry(`conformance/fixtures/${g.tokenizer}`),
  prompt: entry(`conformance/fixtures/${g.prompt}`),
};
writeFileSync(join(pkg, 'example', 'digests.json'), JSON.stringify(out, null, 2) + '\n');
console.log(`example/digests.json: wasm ${out.wasm.sha3_256.slice(0, 16)}..., model ${out.model.sha3_256.slice(0, 16)}...`);
