// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// node_crosscheck.mjs — an independent check of the Z-Wasm fuel meter with
// V8's validator. For each module (and a deterministic set of byte mutants
// of it) that V8 accepts AND the Z-Wasm validator accepts, the metered
// module written by `zw-tool meter` must also pass WebAssembly.validate and
// must export the i64 global __zw_fuel. Node's WebAssembly is the reference
// here, not the host: Z-Wasm runs plugins in wasm3.
//
//   node tools/node_crosscheck.mjs build/zw-tool a.wasm [b.wasm ...]
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [tool, ...files] = process.argv.slice(2);
if (!tool || files.length === 0) {
  console.error('usage: node node_crosscheck.mjs ZW_TOOL FILE.wasm...');
  process.exit(2);
}
const dir = mkdtempSync(join(tmpdir(), 'zw-xcheck-'));
let seed = 0x2545f491;
const rnd = () => {
  seed ^= seed << 13; seed >>>= 0;
  seed ^= seed >>> 17;
  seed ^= seed << 5; seed >>>= 0;
  return seed;
};

function zwAccepts(path) {
  try {
    execFileSync(tool, ['validate', '--grant', 'all', path], { stdio: 'pipe' });
    return true;
  } catch (e) {
    if (e.status === 1) return false;
    throw e;
  }
}

let checked = 0, failures = 0, skipped = 0;
function check(bytes, label) {
  if (!WebAssembly.validate(bytes)) { skipped++; return; }
  const inPath = join(dir, 'in.wasm');
  const outPath = join(dir, 'out.wasm');
  writeFileSync(inPath, bytes);
  if (!zwAccepts(inPath)) { skipped++; return; }
  execFileSync(tool, ['meter', inPath, outPath], { stdio: 'pipe' });
  const out = readFileSync(outPath);
  checked++;
  if (!WebAssembly.validate(out)) {
    failures++;
    writeFileSync(`xcheck-fail-${failures}.wasm`, bytes);
    console.error(`FAIL ${label}: metered module rejected by V8`);
    return;
  }
  const exp = WebAssembly.Module.exports(new WebAssembly.Module(out));
  if (!exp.some((e) => e.name === '__zw_fuel' && e.kind === 'global')) {
    failures++;
    console.error(`FAIL ${label}: no __zw_fuel global export`);
  }
}

for (const f of files) {
  const orig = readFileSync(f);
  check(orig, f);
  for (let i = 0; i < 300; i++) {
    const m = Buffer.from(orig);
    const k = 1 + (rnd() % 3);
    for (let j = 0; j < k; j++) m[rnd() % m.length] = rnd() & 0xff;
    check(m, `${f} mutant ${i}`);
  }
}
rmSync(dir, { recursive: true, force: true });
console.log(`node_crosscheck: ${checked} modules metered and re-validated by V8, ` +
  `${skipped} skipped (invalid in V8 or refused by Z-Wasm), ${failures} failures`);
process.exit(failures ? 1 : 0);
