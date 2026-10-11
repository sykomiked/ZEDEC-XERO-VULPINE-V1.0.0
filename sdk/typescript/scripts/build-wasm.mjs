#!/usr/bin/env node
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Compile libzxv (sdk/libzxv/src/*.c) and the kernel sources that
// sdk/libzxv/Makefile lists into wasm/zxv.wasm: a single-threaded WASI
// "reactor" module that exports every zxv_* function plus malloc/free.
//
// Nothing in sdk/libzxv is modified or built: the source list is read with
// `make -s print-config` (which only echoes variables) and every object
// file goes to build/wasm/ inside this package.
//
// Compiler: $ZXV_WASM_CC if set (any clang driver that targets
// wasm32-wasi with a wasi-libc sysroot, e.g. wasi-sdk's clang), otherwise
// `python3 -m ziglang cc` (pip install ziglang; zig ships wasi-libc).
//
// Options (environment):
//   ZXV_WASM_NO_TENSOR=1   leave out the inference engine (-DZXV_NO_TENSOR)
//   ZXV_WASM_JOBS=n        parallel compile jobs (default: CPU count)
//   ZXV_WASM_CLEAN=1       recompile every object

import { execFileSync, spawn } from 'node:child_process';
import { mkdirSync, existsSync, statSync, copyFileSync } from 'node:fs';
import { cpus } from 'node:os';
import { dirname, join, resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const pkg = resolve(here, '..');
const lib = resolve(pkg, '..', 'libzxv');
const kernel = resolve(lib, '..', '..', 'kernel');
const out = join(pkg, 'build', 'wasm');
const noTensor = process.env.ZXV_WASM_NO_TENSOR === '1';

function compilerCommand() {
  if (process.env.ZXV_WASM_CC) {
    const parts = process.env.ZXV_WASM_CC.split(' ').filter(Boolean);
    return { cmd: parts[0], pre: [...parts.slice(1), '--target=wasm32-wasi'] };
  }
  try {
    execFileSync('python3', ['-m', 'ziglang', 'version'], { stdio: 'ignore' });
  } catch {
    console.error('No wasm compiler: run `python3 -m pip install ziglang`, or set ZXV_WASM_CC.');
    process.exit(2);
  }
  return { cmd: 'python3', pre: ['-m', 'ziglang', 'cc', '-target', 'wasm32-wasi'] };
}

function makeConfig() {
  const text = execFileSync('make', ['-s', '-C', lib, 'print-config', ...(noTensor ? ['ZXV_NO_TENSOR=1'] : [])], {
    encoding: 'utf8',
  });
  const get = (k) => (text.match(new RegExp(`^${k}=(.*)$`, 'm'))?.[1] ?? '').split(/\s+/).filter(Boolean);
  return { defs: get('DEFS'), ksrc: get('KSRC').map((p) => resolve(lib, p)) };
}

const { cmd, pre } = compilerCommand();
const { defs, ksrc } = makeConfig();
// Every kernel include directory the Makefile uses (a superset is harmless).
const incDirs = [
  join(lib, 'include'), join(lib, 'src'),
  ...['iso20022', 'finance', 'vino_stores', 'vino', 'surplus', 'edp_risk', 'onepolicy', 'rational', 'lpres',
    'event_space', 'rmag', 'phase_coord', 'harmonic', 'swarm', 'mlkem', 'e8', 'modbind', 'trispace', 'pqsec',
    'tensor', 'cbank', 'cardnet'].map((d) => join(kernel, 'src', d)),
  join(kernel, 'include'), join(kernel, 'third_party', 'ed25519', 'src'),
].filter((d) => existsSync(d));
const inc = incDirs.flatMap((d) => ['-I', d]);
const shimH = join(pkg, 'wasm', 'zxv_wasm_shim.h');
const common = ['-std=c11', '-O2', '-DTEST_HOST', '-DZXV_HAVE_ARC4RANDOM=1', '-include', shimH, ...defs, ...inc];
if (noTensor && !defs.includes('-DZXV_NO_TENSOR')) common.push('-DZXV_NO_TENSOR');

const units = [
  ...['zxv_api', 'zxv_core', 'zxv_econ', 'zxv_crypto', 'zxv_tensor', 'zxv_evm', 'zxv_hex'].map((n) => ({
    src: join(lib, 'src', `${n}.c`), flags: ['-fvisibility=hidden', '-DZXV_BUILD_SHARED', '-Wall', '-Wno-unused-function'],
  })),
  ...ksrc.map((src) => ({ src, flags: ['-fvisibility=hidden', '-w'] })),
  { src: join(pkg, 'wasm', 'zxv_wasm_shim.c'), flags: ['-fvisibility=hidden', '-Wall', '-Wextra'] },
];

mkdirSync(out, { recursive: true });
const objName = (src) => join(out, relative(resolve(lib, '..', '..'), src).replace(/[\\/]/g, '__').replace(/\.c$/, '.o'));

function run(args) {
  return new Promise((ok, fail) => {
    const p = spawn(cmd, [...pre, ...args], { stdio: 'inherit' });
    p.on('exit', (code) => (code === 0 ? ok() : fail(new Error(`${cmd} exited ${code}: ${args.at(-1)}`))));
  });
}

const jobs = Number(process.env.ZXV_WASM_JOBS ?? cpus().length) || 1;
const queue = units.slice();
const objs = units.map((u) => objName(u.src));
// An object is reused only if it is newer than its source and than the
// headers every unit sees (ZXV_WASM_CLEAN=1 rebuilds everything).
const headerTime = Math.max(...[shimH, join(lib, 'include', 'zxv_api.h'), join(lib, 'src', 'zxv_internal.h')].map((h) => statSync(h).mtimeMs));
const fresh = (o, src) =>
  process.env.ZXV_WASM_CLEAN !== '1' && existsSync(o) && statSync(o).mtimeMs > Math.max(statSync(src).mtimeMs, headerTime);
async function worker() {
  for (let u = queue.shift(); u; u = queue.shift()) {
    const o = objName(u.src);
    if (fresh(o, u.src)) continue;
    await run([...common, ...u.flags, '-c', u.src, '-o', o]);
  }
}
await Promise.all(Array.from({ length: jobs }, worker));

const wasm = join(out, 'zxv.wasm');
await run([
  '-mexec-model=reactor', '-O2', '-o', wasm, ...objs,
  '-Wl,--export-dynamic', '-Wl,--export=malloc', '-Wl,--export=free',
  '-Wl,-z,stack-size=1048576', '-Wl,--initial-memory=4194304',
]);
copyFileSync(wasm, join(pkg, 'wasm', 'zxv.wasm'));
console.log(`wasm/zxv.wasm: ${statSync(wasm).size} bytes (${units.length} units${noTensor ? ', no tensor' : ''}, single-threaded)`);
