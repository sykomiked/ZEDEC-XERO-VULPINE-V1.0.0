#!/usr/bin/env node
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// npm install hook: build the Node-API addon if it is not there yet. A
// failure is not fatal: Zxv.load() then falls back to the WebAssembly
// backend, and `npm run build:native` can be retried later.

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const pkg = resolve(dirname(fileURLToPath(import.meta.url)), '..');
if (existsSync(join(pkg, 'native', 'build', 'Release', 'zxv_addon.node')) || process.env.ZXV_SKIP_NATIVE === '1') process.exit(0);
const gyp = spawnSync('npx', ['--no-install', 'node-gyp', 'rebuild', '--directory', 'native'], { cwd: pkg, stdio: 'inherit', shell: process.platform === 'win32' });
if (gyp.status !== 0) {
  console.warn('@zedec/zxv: the native addon was not built; the WebAssembly backend will be used. Retry with `npm run build:native`.');
}
