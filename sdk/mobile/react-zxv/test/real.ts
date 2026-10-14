// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Paths to the real artifacts the integration tests use: the fixture
// model (make -C sdk/libzxv fixtures) and the wasm module
// (sdk/typescript: npm run build:wasm).

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const sdk = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const vectors = JSON.parse(readFileSync(join(sdk, 'conformance', 'vectors.json'), 'utf8')) as {
  generate: { model: string; tokenizer: string; prompt: string; n_ctx: number; greedy: { max_tokens: number; tokens: number[] } };
};

export const G = vectors.generate;
export const fixtures = join(sdk, 'conformance', 'fixtures');
export const modelPath = join(fixtures, G.model);
export const tokenizerPath = join(fixtures, G.tokenizer);
export const prompt = readFileSync(join(fixtures, G.prompt), 'utf8');
export const wasmPath = join(sdk, 'typescript', 'wasm', 'zxv.wasm');
