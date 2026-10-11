#!/usr/bin/env node
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Stage what a published @zedec/zxv carries besides dist/: the C header the
// addon compiles against (native/include/zxv_api.h) and, when it has been
// built, the bundled libzxv (lib/), which the loader finds after
// ZXV_LIBRARY_PATH (SDK_DESIGN.md §1).

import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const pkg = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const lib = resolve(pkg, '..', 'libzxv');
mkdirSync(join(pkg, 'native', 'include'), { recursive: true });
copyFileSync(join(lib, 'include', 'zxv_api.h'), join(pkg, 'native', 'include', 'zxv_api.h'));
mkdirSync(join(pkg, 'lib'), { recursive: true });
for (const name of ['libzxv.so', 'libzxv.dylib', 'zxv.dll']) {
  const src = join(lib, 'build', name);
  if (existsSync(src)) copyFileSync(src, join(pkg, 'lib', name));
}
if (!existsSync(join(pkg, 'wasm', 'zxv.wasm'))) {
  console.error('wasm/zxv.wasm is missing: run `npm run build:wasm` before packing');
  process.exit(1);
}
