// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The Node-API backend: native/zxv_addon.cc opening libzxv with dlopen.
// Node-only; the browser entry point never imports this file.

import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import type { Arg, Backend, GenerateCall, GenerateJob, GenerateReturn, HwHandler, Ptr } from './backend.js';

interface Addon {
  load(path: string): AddonLib;
}

type AddonFn = (...args: Arg[]) => number;

interface AddonLib {
  [fn: string]: unknown;
  alloc(bytes: number): bigint;
  free(ptr: bigint, bytes: number): void;
  read(ptr: bigint, bytes: number): Uint8Array;
  write(ptr: bigint, data: Uint8Array): void;
  zxv_status_name(code: number): string;
  zxv_lp_name(value: number): string;
  zxv_last_error(buf: Uint8Array | null, cap: number, outLen: BigUint64Array): number;
  zxvjs_generate(
    cap: Ptr, session: Ptr, prompt: Uint8Array, maxTokens: number, temp: number, topK: number, topP: number,
    rep: number, repN: number, seed: bigint,
    onToken: (id: number, piece: Uint8Array) => boolean,
    onDone: (status: number, nPrompt: number, nGenerated: number, stop: number, detail: string) => void,
  ): () => void;
  zxvjs_hw_handler(
    dispatch: (kind: number, id: number, a: number, b: number, c: number, d: number, data: Uint8Array | null, outCap: number) =>
      number | Uint8Array,
  ): void;
  path: string;
}

/** Options for locating the native pieces. */
export interface NativeLocation {
  /** Explicit path to libzxv (.so / .dylib / .dll). */
  libraryPath?: string;
  /** Explicit path to zxv_addon.node. */
  addonPath?: string;
}

const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');

function libFileName(): string {
  if (process.platform === 'darwin') return 'libzxv.dylib';
  if (process.platform === 'win32') return 'zxv.dll';
  return 'libzxv.so';
}

/**
 * The library search order of SDK_DESIGN.md §1: explicit path,
 * ZXV_LIBRARY_PATH, the bundled copy, then the system loader. "Bundled" is
 * ../libzxv/build when running inside this repository (checked first, so a
 * stale staged copy never shadows a fresh build), else lib/ of a published
 * package.
 */
export function locateLibrary(explicit?: string): string {
  if (explicit) return explicit;
  const env = process.env['ZXV_LIBRARY_PATH'];
  if (env) return env;
  const name = libFileName();
  for (const p of [join(pkgRoot, '..', 'libzxv', 'build', name), join(pkgRoot, 'lib', name)]) {
    if (existsSync(p)) return p;
  }
  return name; // let dlopen search the system path
}

function locateAddon(explicit?: string): string {
  const candidates = [
    explicit,
    process.env['ZXV_ADDON_PATH'],
    join(pkgRoot, 'native', 'build', 'Release', 'zxv_addon.node'),
    join(pkgRoot, 'native', 'build', 'Debug', 'zxv_addon.node'),
    join(pkgRoot, 'prebuilds', `${process.platform}-${process.arch}`, 'zxv_addon.node'),
  ];
  for (const c of candidates) if (c && existsSync(c)) return c;
  throw new Error('zxv_addon.node not found: run `npm run build:native` (needs a C++17 compiler and node-gyp)');
}

class NativeBackend implements Backend {
  readonly kind = 'native' as const;
  readonly NULL: Ptr = 0n;
  readonly origin: string;
  readonly #lib: AddonLib;
  readonly #fns = new Map<string, AddonFn>();

  constructor(lib: AddonLib) {
    this.#lib = lib;
    this.origin = lib.path;
  }

  call(fn: string, ...args: Arg[]): number {
    let f = this.#fns.get(fn);
    if (!f) {
      const g = this.#lib[fn];
      if (typeof g !== 'function') throw new Error(`libzxv has no function ${fn}`);
      f = g as AddonFn;
      this.#fns.set(fn, f);
    }
    const r = f(...args);
    return typeof r === 'number' ? r : 0;
  }

  statusName(code: number): string {
    return this.#lib.zxv_status_name(code);
  }

  lpName(value: number): string {
    return this.#lib.zxv_lp_name(value);
  }

  lastError(): string {
    const len = new BigUint64Array(1);
    this.#lib.zxv_last_error(null, 0, len);
    const n = Number(len[0]);
    if (n === 0) return '';
    const buf = new Uint8Array(n + 1);
    if (this.#lib.zxv_last_error(buf, n + 1, len) !== 0) return '';
    return new TextDecoder().decode(buf.subarray(0, n));
  }

  handleSlot(): BigUint64Array {
    return new BigUint64Array(1);
  }

  readHandle(slot: BigUint64Array | Uint32Array): Ptr {
    return BigInt(slot[0] ?? 0);
  }

  sizeSlot(): BigUint64Array {
    return new BigUint64Array(1);
  }

  readSize(slot: BigUint64Array | Uint32Array): number {
    return Number(slot[0] ?? 0);
  }

  alloc(bytes: number): Ptr {
    return this.#lib.alloc(bytes);
  }

  free(ptr: Ptr, bytes: number): void {
    this.#lib.free(BigInt(ptr), bytes);
  }

  read(ptr: Ptr, bytes: number): Uint8Array {
    return this.#lib.read(BigInt(ptr), bytes);
  }

  write(ptr: Ptr, data: Uint8Array): void {
    this.#lib.write(BigInt(ptr), data);
  }

  isNull(ptr: Ptr): boolean {
    return BigInt(ptr) === 0n;
  }

  setHwHandler(h: HwHandler): void {
    // Kinds as in native/zxv_addon.cc: 0 call, 1 load, 2 readback, 3 activate, 4 event.
    this.#lib.zxvjs_hw_handler((kind, id, a, b, c, d, data, outCap) => {
      switch (kind) {
        case 0: return h.call(id, a, data ?? new Uint8Array(0), outCap);
        case 1: return h.load(id, a, data ?? new Uint8Array(0));
        case 2: return h.readback(id, a);
        case 3: return h.activate(id, a);
        case 4: h.event(id, a, b | 0, c, d); return 0;
        default: return -9;
      }
    });
  }

  generate(c: GenerateCall, onToken: (id: number, piece: Uint8Array) => boolean): GenerateJob {
    let cancel: () => void = () => {};
    const done = new Promise<GenerateReturn>((ok) => {
      cancel = this.#lib.zxvjs_generate(
        c.cap, c.session, c.prompt, c.maxTokens, c.temperatureQ16, c.topK, c.topPQ16, c.repeatPenaltyQ16,
        c.repeatLastN, c.seed, onToken,
        (status, nPrompt, nGenerated, stop, detail) => ok({ status, nPrompt, nGenerated, stop, detail }),
      );
    });
    return { done, cancel: () => cancel() };
  }
}

const loaded = new Map<string, NativeBackend>();

/** Load (once per library path) the addon backend. */
export function loadNative(loc: NativeLocation = {}): Backend {
  const libPath = locateLibrary(loc.libraryPath);
  const hit = loaded.get(libPath);
  if (hit) return hit;
  const require = createRequire(import.meta.url);
  const addon = require(locateAddon(loc.addonPath)) as Addon;
  const b = new NativeBackend(addon.load(libPath));
  loaded.set(libPath, b);
  return b;
}
