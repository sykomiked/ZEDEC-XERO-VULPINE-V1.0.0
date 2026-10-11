// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The browser entry point: its module graph imports no Node built-ins, and
// it runs the engine deterministically from wasm bytes alone (as a page or
// Worker would, after fetching zxv.wasm and the model).

import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { test } from 'node:test';

import { fixtures, pkgRoot, V } from './helpers.js';

const dist = join(pkgRoot, 'dist', 'src');

test('the browser module graph has no Node built-ins', () => {
  const seen = new Set<string>();
  const visit = (file: string) => {
    if (seen.has(file)) return;
    seen.add(file);
    const src = readFileSync(file, 'utf8');
    for (const m of src.matchAll(/(?:import|export)[^'"]*from\s*['"]([^'"]+)['"]/g)) {
      const spec = m[1]!;
      assert.ok(spec.startsWith('.'), `${file} imports ${spec}`);
      visit(resolve(dirname(file), spec));
    }
  };
  visit(join(dist, 'browser.js'));
  assert.ok(seen.size > 10);
  assert.ok(![...seen].some((f) => f.endsWith('native.js')), 'the addon loader is not reachable');
});

test('the browser entry generates the vectors tokens from bytes alone', () => {
  const code = `
    import { readFileSync } from 'node:fs';
    const { Zxv, Model, Cap } = await import(${JSON.stringify(pathToFileURL(join(dist, 'browser.js')).href)});
    const zxv = await Zxv.load({ wasm: readFileSync(${JSON.stringify(join(pkgRoot, 'wasm', 'zxv.wasm'))}) });
    const ctx = zxv.createContext();
    const model = Model.fromBytes(ctx.rootCapability({ caps: Cap.AI_INFERENCE }),
      readFileSync(${JSON.stringify(join(fixtures, V.generate.model))}), readFileSync(${JSON.stringify(join(fixtures, V.generate.tokenizer))}));
    const s = model.newSession({ nCtx: 128 });
    const r = await s.generate(readFileSync(${JSON.stringify(join(fixtures, V.generate.prompt))}, 'utf8'), { maxTokens: 16 });
    ctx.close();
    console.log(JSON.stringify({ backend: zxv.backend, tokens: r.tokens }));`;
  const r = spawnSync(process.execPath, ['--input-type=module', '-e', code], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  const out = JSON.parse(r.stdout) as { backend: string; tokens: number[] };
  assert.equal(out.backend, 'wasm');
  assert.deepEqual(out.tokens, V.generate.greedy.tokens);
});
