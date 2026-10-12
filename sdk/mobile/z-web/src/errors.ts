// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Errors of the page side. A libzxv failure in the worker arrives with its
// status, symbolic name and class (zxv_status_class), the same data zxvd
// puts in a JSON-RPC error: branch on `status` or `statusClass`, never on
// the message.

/** zxv_status_class names. */
export type StatusClassName = 'USAGE' | 'REFUSED' | 'FATAL' | 'SYSTEM';

/** What a wire error carries. */
export interface WireError {
  /** The original error class (e.g. "CapabilityDeniedError", "BudgetExhaustedError"). */
  readonly name: string;
  readonly message: string;
  /** ZXV_E_* number, when it came from libzxv or the wire checks. */
  readonly status?: number;
  /** ZXV_E_* name. */
  readonly statusName?: string;
  readonly statusClass?: StatusClassName;
  /** zxv_last_error text. */
  readonly detail?: string;
}

/** Fields of a {@link ZxvWebError}. */
export interface ZxvWebErrorInit {
  name?: string;
  status?: number;
  statusName?: string;
  statusClass?: StatusClassName;
  detail?: string;
}

/**
 * An error from the worker or from the wire checks. `kind` is the original
 * class name (`CapabilityDeniedError`, `BudgetExhaustedError`,
 * `FloatRejectedError`, ...). A `statusClass` of FATAL means the worker's
 * context is poisoned: close it, never retry.
 */
export class ZxvWebError extends Error {
  readonly kind: string;
  readonly status: number | undefined;
  readonly statusName: string | undefined;
  readonly statusClass: StatusClassName | undefined;
  readonly detail: string | undefined;

  constructor(message: string, init: ZxvWebErrorInit = {}) {
    super(message);
    this.name = 'ZxvWebError';
    this.kind = init.name ?? 'Error';
    this.status = init.status;
    this.statusName = init.statusName;
    this.statusClass = init.statusClass;
    this.detail = init.detail;
  }

  /** True for a FATAL class error (the context can only be closed). */
  get fatal(): boolean {
    return this.statusClass === 'FATAL';
  }

  /** Rebuild from the wire. */
  static fromWire(e: WireError): ZxvWebError {
    const init: ZxvWebErrorInit = { name: e.name };
    if (e.status !== undefined) init.status = e.status;
    if (e.statusName !== undefined) init.statusName = e.statusName;
    if (e.statusClass !== undefined) init.statusClass = e.statusClass;
    if (e.detail !== undefined) init.detail = e.detail;
    return new ZxvWebError(e.message, init);
  }
}

/** A file whose SHA3-256 is not the expected digest. It is never used or cached. */
export class DigestMismatchError extends Error {
  readonly expected: string;
  readonly actual: string;
  constructor(what: string, expected: string, actual: string) {
    super(`${what}: SHA3-256 is ${actual}, expected ${expected}; refusing to use it`);
    this.name = 'DigestMismatchError';
    this.expected = expected;
    this.actual = actual;
  }
}

/** The worker died (an uncaught error, or terminate()). Every pending call rejects with it. */
export class WorkerGoneError extends Error {
  constructor(reason: string) {
    super(`zxv worker is gone: ${reason}`);
    this.name = 'WorkerGoneError';
  }
}
