// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The WebAssembly backend: wasm/zxv.wasm (libzxv + kernel compiled to
// wasm32-wasi by scripts/build-wasm.mjs), instantiated with plain
// WebAssembly APIs and a minimal WASI shim, so it runs unchanged in
// browsers, Workers, Deno, Bun and Node. It is single-threaded and
// strictly deterministic: no clock, no environment, no files; the only
// nondeterministic import is random_get (crypto.getRandomValues), used only
// by key generation and hedged signing.

import type { Arg, Backend, GenerateCall, GenerateJob, GenerateReturn, Ptr } from './backend.js';

/** What the wasm module can be loaded from. */
export type WasmSource = BufferSource | WebAssembly.Module;

type WasmFn = (...args: (number | bigint)[]) => number;

interface Exports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  malloc(n: number): number;
  free(p: number): void;
  [fn: string]: unknown;
}

/** Thrown if the C code calls exit() (it never should). */
class WasiExit extends Error {
  constructor(readonly code: number) {
    super(`libzxv (wasm) called exit(${code})`);
  }
}

const ERRNO_SUCCESS = 0;
const ERRNO_BADF = 8;
const ERRNO_NOENT = 44;
const ERRNO_NOSYS = 52;

class WasmBackend implements Backend {
  readonly kind = 'wasm' as const;
  readonly origin = 'wasm';
  readonly NULL: Ptr = 0;
  #ex!: Exports;
  #fns = new Map<string, WasmFn>();
  #onToken: ((id: number, piece: number, len: number) => number) | null = null;
  #lines: Record<number, string> = { 1: '', 2: '' };

  /** The imports object (WASI shim + the token callback). */
  imports(): WebAssembly.Imports {
    const view = () => new DataView(this.#ex.memory.buffer);
    const bytes = () => new Uint8Array(this.#ex.memory.buffer);
    const wasi = {
      fd_write: (fd: number, iovs: number, n: number, nwritten: number): number => {
        const v = view();
        let total = 0;
        let text = '';
        for (let i = 0; i < n; i++) {
          const p = v.getUint32(iovs + 8 * i, true);
          const l = v.getUint32(iovs + 8 * i + 4, true);
          text += new TextDecoder().decode(bytes().subarray(p, p + l));
          total += l;
        }
        v.setUint32(nwritten, total, true);
        if (fd === 1 || fd === 2) {
          const all = (this.#lines[fd] ?? '') + text;
          const parts = all.split('\n');
          this.#lines[fd] = parts.pop() ?? '';
          for (const line of parts) (fd === 2 ? console.error : console.log)(`[libzxv] ${line}`);
          return ERRNO_SUCCESS;
        }
        return ERRNO_BADF;
      },
      random_get: (p: number, n: number): number => {
        for (let off = 0; off < n; off += 65536) {
          globalThis.crypto.getRandomValues(bytes().subarray(p + off, p + Math.min(n, off + 65536)));
        }
        return ERRNO_SUCCESS;
      },
      proc_exit: (code: number): never => {
        throw new WasiExit(code);
      },
      fd_close: () => ERRNO_BADF,
      fd_fdstat_get: () => ERRNO_BADF,
      fd_fdstat_set_flags: () => ERRNO_BADF,
      fd_prestat_get: () => ERRNO_BADF, // no preopened directories: no file system
      fd_prestat_dir_name: () => ERRNO_BADF,
      fd_read: () => ERRNO_BADF,
      fd_seek: () => ERRNO_BADF,
      path_open: () => ERRNO_NOENT,
      environ_get: () => ERRNO_SUCCESS,
      environ_sizes_get: (count: number, size: number): number => {
        view().setUint32(count, 0, true);
        view().setUint32(size, 0, true);
        return ERRNO_SUCCESS;
      },
      clock_time_get: () => ERRNO_NOSYS, // deliberately absent: determinism
    };
    const zxv = {
      on_token: (_user: number, token: number, piece: number, len: number): number =>
        this.#onToken ? this.#onToken(token, piece, len) : 0,
    };
    return { wasi_snapshot_preview1: wasi, zxv };
  }

  attach(instance: WebAssembly.Instance): void {
    this.#ex = instance.exports as unknown as Exports;
    this.#ex._initialize?.();
  }

  #fn(name: string): WasmFn {
    let f = this.#fns.get(name);
    if (!f) {
      const g = this.#ex[name];
      if (typeof g !== 'function') throw new Error(`zxv.wasm has no function ${name}`);
      f = g as WasmFn;
      this.#fns.set(name, f);
    }
    return f;
  }

  call(fn: string, ...args: Arg[]): number {
    const f = this.#fn(fn);
    const temps: { view: ArrayBufferView; ptr: number; len: number }[] = [];
    try {
      const conv = args.map((a) => {
        if (a === null) return 0;
        if (ArrayBuffer.isView(a)) {
          const len = a.byteLength;
          const ptr = this.#ex.malloc(len || 1);
          if (!ptr) throw new RangeError('wasm out of memory');
          new Uint8Array(this.#ex.memory.buffer, ptr, len).set(new Uint8Array(a.buffer, a.byteOffset, len));
          temps.push({ view: a, ptr, len });
          return ptr;
        }
        return a;
      });
      const r = f(...conv);
      for (const t of temps) {
        new Uint8Array(t.view.buffer, t.view.byteOffset, t.len).set(new Uint8Array(this.#ex.memory.buffer, t.ptr, t.len));
      }
      return typeof r === 'number' ? r : 0;
    } finally {
      for (const t of temps) {
        new Uint8Array(this.#ex.memory.buffer, t.ptr, t.len).fill(0);
        this.#ex.free(t.ptr);
      }
    }
  }

  #cString(p: number): string {
    const b = new Uint8Array(this.#ex.memory.buffer);
    let e = p;
    while (b[e] !== 0) e++;
    return new TextDecoder().decode(b.subarray(p, e));
  }

  statusName(code: number): string {
    return this.#cString(this.#fn('zxv_status_name')(code));
  }

  lpName(value: number): string {
    return this.#cString(this.#fn('zxv_lp_name')(value));
  }

  lastError(): string {
    const len = new Uint32Array(1);
    this.call('zxv_last_error', null, 0, len);
    const n = len[0] ?? 0;
    if (n === 0) return '';
    const buf = new Uint8Array(n + 1);
    if (this.call('zxv_last_error', buf, n + 1, len) !== 0) return '';
    return new TextDecoder().decode(buf.subarray(0, n));
  }

  handleSlot(): Uint32Array {
    return new Uint32Array(1);
  }

  readHandle(slot: BigUint64Array | Uint32Array): Ptr {
    return Number(slot[0] ?? 0);
  }

  sizeSlot(): Uint32Array {
    return new Uint32Array(1);
  }

  readSize(slot: BigUint64Array | Uint32Array): number {
    return Number(slot[0] ?? 0);
  }

  alloc(bytes: number): Ptr {
    const p = this.#ex.malloc(bytes || 1);
    if (!p) throw new RangeError('wasm out of memory');
    new Uint8Array(this.#ex.memory.buffer, p, bytes).fill(0);
    return p;
  }

  free(ptr: Ptr, bytes: number): void {
    const p = Number(ptr);
    if (!p) return;
    this.#fn('zxv_secure_zero')(p, bytes);
    this.#ex.free(p);
  }

  read(ptr: Ptr, bytes: number): Uint8Array {
    return new Uint8Array(this.#ex.memory.buffer, Number(ptr), bytes).slice();
  }

  write(ptr: Ptr, data: Uint8Array): void {
    new Uint8Array(this.#ex.memory.buffer, Number(ptr), data.length).set(data);
  }

  isNull(ptr: Ptr): boolean {
    return Number(ptr) === 0;
  }

  generate(c: GenerateCall, onToken: (id: number, piece: Uint8Array) => boolean): GenerateJob {
    let cancelled = false;
    let thrown: unknown = undefined;
    const out = new Uint32Array(3);
    this.#onToken = (id, piece, len) => {
      try {
        if (!onToken(id, new Uint8Array(this.#ex.memory.buffer, piece, len).slice())) cancelled = true;
      } catch (e) {
        thrown = e;
        return 0;
      }
      return cancelled ? 0 : 1;
    };
    let status: number;
    try {
      status = this.call(
        'zxvjs_generate', Number(c.cap), Number(c.session), c.prompt, c.prompt.length, c.maxTokens,
        c.temperatureQ16, c.topK, c.topPQ16, c.repeatPenaltyQ16, c.repeatLastN, c.seed, 0,
        out.subarray(0, 1), out.subarray(1, 2), out.subarray(2, 3),
      );
    } finally {
      this.#onToken = null;
    }
    if (thrown !== undefined) throw thrown;
    const r: GenerateReturn = {
      status,
      nPrompt: out[0] ?? 0,
      nGenerated: out[1] ?? 0,
      stop: (out[2] ?? 0) | 0,
      detail: status === 0 ? '' : this.lastError(),
    };
    return { done: Promise.resolve(r), cancel: () => { cancelled = true; } };
  }
}

/** Instantiate the wasm backend from the module bytes (or a compiled module). */
export async function loadWasm(source: WasmSource): Promise<Backend> {
  const b = new WasmBackend();
  const module = source instanceof WebAssembly.Module ? source : await WebAssembly.compile(source);
  const instance = await WebAssembly.instantiate(module, b.imports());
  b.attach(instance);
  return b;
}
