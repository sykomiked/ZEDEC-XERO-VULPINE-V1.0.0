// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The one low-level interface both backends implement. Everything public in
// this package is written once against it, so the Node-API addon and the
// WebAssembly module behave identically by construction.
//
// Argument conventions for `call` (the C type decides):
//   int8..int32, uint8..uint32, size_t   -> number (an integer)
//   int64_t, uint64_t                    -> bigint
//   any pointer                          -> Ptr (handle / backend memory),
//                                           an ArrayBufferView (copied in and
//                                           back out around the call by the
//                                           wasm backend; pinned by the
//                                           addon), or null
//
// This module is internal: nothing here is exported from the package.

/** An address in the backend's memory: a bigint for the addon, a number for wasm. */
export type Ptr = bigint | number;

/** One argument of a C call; see the conventions above. */
export type Arg = number | bigint | ArrayBufferView | null;

/** The parameters of one zxv_generate call, already converted to the C types. */
export interface GenerateCall {
  cap: Ptr;
  session: Ptr;
  prompt: Uint8Array;
  maxTokens: number;
  temperatureQ16: number;
  topK: number;
  topPQ16: number;
  repeatPenaltyQ16: number;
  repeatLastN: number;
  seed: bigint;
}

/** What zxv_generate returned. */
export interface GenerateReturn {
  status: number;
  nPrompt: number;
  nGenerated: number;
  stop: number;
  detail: string;
}

/** A running generation. */
export interface GenerateJob {
  readonly done: Promise<GenerateReturn>;
  /** Ask the engine to stop after the current token (stop reason CALLBACK). */
  cancel(): void;
}

/**
 * Receives the calls libzxv makes into JS hardware implementations and
 * region devices (ABI 1.4), identified by the uint32 id the caller passed
 * as `user`. Called synchronously on the calling thread; it must not throw.
 */
export interface HwHandler {
  /** An implementation: the output bytes, or a negative status. */
  call(id: number, op: number, input: Uint8Array, outCap: number): number | Uint8Array;
  load(id: number, slot: number, payload: Uint8Array): number;
  /** The SHA-256 of what the slot holds (32 bytes), or a negative status. */
  readback(id: number, slot: number): number | Uint8Array;
  activate(id: number, slot: number): number;
  event(id: number, regionId: number, event: number, reason: number, version: number): void;
}

export interface Backend {
  readonly kind: 'native' | 'wasm';
  /** Where the library came from (a file path, or "wasm"). */
  readonly origin: string;
  /** The NULL address in this backend's representation. */
  readonly NULL: Ptr;
  /** Call a zxv_* function returning int32 (or void, then 0). */
  call(fn: string, ...args: Arg[]): number;
  statusName(code: number): string;
  lpName(value: number): string;
  /** zxv_last_error for the calling thread ("" if none). */
  lastError(): string;
  /** A one-element out slot for a handle (T **). */
  handleSlot(): BigUint64Array | Uint32Array;
  readHandle(slot: BigUint64Array | Uint32Array): Ptr;
  /** A one-element out slot for a size_t. */
  sizeSlot(): BigUint64Array | Uint32Array;
  readSize(slot: BigUint64Array | Uint32Array): number;
  /** Allocate zeroed backend memory (outside the JS heap). */
  alloc(bytes: number): Ptr;
  /** Wipe with zxv_secure_zero, then free. */
  free(ptr: Ptr, bytes: number): void;
  /** Copy bytes out of backend memory. */
  read(ptr: Ptr, bytes: number): Uint8Array;
  /** Copy bytes into backend memory. */
  write(ptr: Ptr, data: Uint8Array): void;
  /**
   * Run zxv_generate. The addon runs it on a worker thread and streams the
   * tokens to `onToken` on the event loop; wasm runs it synchronously on
   * the calling thread (call it from a Worker in a browser). `onToken`
   * returns false to stop after this token.
   */
  generate(call: GenerateCall, onToken: (id: number, piece: Uint8Array) => boolean): GenerateJob;
  /** True if `ptr` is this backend's NULL. */
  isNull(ptr: Ptr): boolean;
  /**
   * Install the receiver of hardware callbacks (one per backend), used by
   * the zxvjs_hw_provider_add / zxvjs_hw_region_create /
   * zxvjs_hw_region_events entry points.
   */
  setHwHandler(h: HwHandler): void;
}

const enc = new TextEncoder();

/** UTF-8 encode a string with a terminating NUL for a `const char *`. */
export function cstr(s: string): Uint8Array {
  const b = enc.encode(s);
  const out = new Uint8Array(b.length + 1);
  out.set(b);
  return out;
}

/** UTF-8 encode without a NUL. */
export function utf8(s: string): Uint8Array {
  return enc.encode(s);
}

const dec = new TextDecoder('utf-8', { fatal: false });

/** Decode the first `len` bytes of `buf` as UTF-8. */
export function fromUtf8(buf: Uint8Array, len = buf.length): string {
  return dec.decode(buf.subarray(0, len));
}

/** Standard ZXV_E_BUFFER code (R4 two-call pattern). */
export const E_BUFFER = -2;
