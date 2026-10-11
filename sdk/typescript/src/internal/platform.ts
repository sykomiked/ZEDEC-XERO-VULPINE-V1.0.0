// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// What differs between the Node and browser entry points. Each entry point
// installs its platform when it is imported.

import type { Backend } from './backend.js';
import type { WasmSource } from './wasm.js';

/** Options for loading a backend (see Zxv.load). */
export interface LoadOptions {
  /** 'native' (Node-API addon), 'wasm', or 'auto' (native if it loads, else wasm; the default in Node). */
  backend?: 'native' | 'wasm' | 'auto';
  /** Explicit path to libzxv (native backend). */
  libraryPath?: string;
  /** Explicit path to zxv_addon.node (native backend). */
  addonPath?: string;
  /** The wasm module: bytes, a compiled module, or a URL/path to fetch or read. */
  wasm?: WasmSource | URL | string;
}

export interface Platform {
  readonly name: 'node' | 'browser';
  loadNative?(options: LoadOptions): Backend;
  /** Bytes of the default zxv.wasm, or of a URL/path given in options. */
  wasmBytes(where?: URL | string): Promise<WasmSource>;
  /** Read a file or URL (Model.open on the wasm backend). */
  readFile(where: URL | string): Promise<Uint8Array>;
}

let current: Platform | undefined;

export function setPlatform(p: Platform): void {
  current ??= p;
}

export function platform(): Platform {
  if (!current) throw new Error('@zedec/zxv: import the package entry point ("@zedec/zxv"), not an internal module');
  return current;
}
