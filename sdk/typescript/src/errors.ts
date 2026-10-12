// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

/**
 * Every ZXV status code (zxv_api.h). Codes are stable and never
 * renumbered; an unknown negative code is still an error (R8).
 */
export const Status = Object.freeze({
  OK: 0,
  E_ARG: -1,
  E_BUFFER: -2,
  E_NOTIMPL: -3,
  E_NOT_FOUND: -4,
  E_CAPACITY: -5,
  E_FUNDS: -6,
  E_CURRENCY: -7,
  E_RANGE: -8,
  E_STATE: -9,
  E_INVARIANT: -10,
  E_UTF8: -11,
  E_TOO_LONG: -12,
  E_DUPLICATE: -13,
  E_NOMEM: -14,
  E_DECLINED: -15,
  E_FIELD: -16,
  E_CAP: -17,
  E_POISONED: -18,
  E_BUDGET: -19,
  E_RMAG_INVALID: -20,
  E_VERIFY: -21,
  E_FORMAT: -22,
  E_IO: -23,
  E_FLOAT: -24,
  /** No proven implementation may serve the call (ABI 1.4). */
  E_NOPROVIDER: -25,
  /** A shadow check found two implementations disagree (ABI 1.4). */
  E_DISPUTE: -26,
  /** The image failed in hardware: rolled back and logged (ABI 1.4). */
  E_QUARANTINED: -27,
  /** Image or contract not newer, or below the floor (ABI 1.4). */
  E_ROLLBACK: -28,
  /** The image is quarantined and never reloaded (ABI 1.4). */
  E_BLOCKED: -29,
  /** The image is for other hardware (ABI 1.4). */
  E_TARGET: -30,
  /** Refused by the function's placement policy (ABI 1.4). */
  E_POLICY: -31,
  E_INTERNAL: -99,
} as const);

/** The four classes of failure (zxv_status_class). */
export const StatusClass = Object.freeze({
  OK: 0,
  USAGE: 1,
  REFUSED: 2,
  FATAL: 3,
  SYSTEM: 4,
} as const);

/** A StatusClass value. */
export type StatusClass = (typeof StatusClass)[keyof typeof StatusClass];

/** Fields shared by every error raised for a ZXV status. */
export interface ZxvStatusInfo {
  /** The negative ZXV_E_* code. */
  readonly status: number;
  /** Its symbolic name, e.g. "ZXV_E_FUNDS". */
  readonly statusName: string;
  /** zxv_last_error text: diagnostic only, never branch on it. */
  readonly detail: string;
}

/**
 * Base class of the ordinary (catchable, handle-it) ZXV errors. Branch on
 * the subclass or on `status`, never on the message.
 */
export abstract class ZxvError extends Error implements ZxvStatusInfo {
  readonly status: number;
  readonly statusName: string;
  readonly detail: string;
  /** Always false for ordinary errors; see {@link ZxvFatalError}. */
  readonly fatal: boolean = false;

  constructor(status: number, statusName: string, detail: string, message?: string) {
    super(message ?? (detail ? `${statusName}: ${detail}` : statusName));
    this.name = new.target.name;
    this.status = status;
    this.statusName = statusName;
    this.detail = detail;
  }
}

/** USAGE class: a caller bug. Fix the arguments; retrying fails again. */
export class ZxvUsageError extends ZxvError {}

/**
 * A host floating-point value reached an exact boundary: a non-integer
 * `number` where an integer is required, or NaN/infinity in
 * `Rational.fromNumberExact`. Use `Rational.fromNumberExact(x)` to convert
 * a float on purpose, or pass a bigint / integer / exact decimal string.
 */
export class FloatRejectedError extends ZxvUsageError {
  constructor(detail: string) {
    super(Status.E_FLOAT, 'ZXV_E_FLOAT', detail);
  }
}

/** REFUSED class: a well-formed request the platform declined. Handle it. */
export class ZxvRefusedError extends ZxvError {}
/** The capability lacks the bit, is revoked, or its attestation is not TRUE. */
export class CapabilityDeniedError extends ZxvRefusedError {}
/** An enforced rmag or swarm budget is exhausted. Retry after a release. */
export class BudgetExhaustedError extends ZxvRefusedError {}
/** A holder account would go below zero. */
export class InsufficientFundsError extends ZxvRefusedError {}
/** A signature or tag did not verify. */
export class VerificationFailedError extends ZxvRefusedError {}
/** The feature is not built into this library (see Zxv.features). */
export class NotImplementedError extends ZxvRefusedError {}

/** SYSTEM class: out of memory, I/O, internal. */
export class ZxvSystemError extends ZxvError {}

/**
 * FATAL class: the context broke an invariant (or was halted) and is
 * poisoned in C: every later call on it fails, whatever this process does.
 *
 * It deliberately does NOT extend {@link ZxvError}, so `catch (e) { if (e
 * instanceof ZxvError) retry() }` can never swallow it. Never retry; drop
 * the context. Use `fatalPolicy: 'abort'` to terminate the process instead.
 */
export class ZxvFatalError extends Error implements ZxvStatusInfo {
  /** Always true. */
  readonly fatal = true as const;
  readonly status: number;
  readonly statusName: string;
  readonly detail: string;

  constructor(status: number, statusName: string, detail: string) {
    super(`FATAL ${statusName}: ${detail || 'the context is halted'}; it can only be closed`);
    this.name = 'ZxvFatalError';
    this.status = status;
    this.statusName = statusName;
    this.detail = detail;
  }
}

/**
 * What a context does on a FATAL status: `'throw'` raises
 * {@link ZxvFatalError} (the default); `'abort'` terminates the process at
 * once with `process.abort()` (Node; in a browser, where nothing can end
 * the page, it throws), the only truly uncatchable form.
 */
export type FatalPolicy = 'throw' | 'abort';

/** Map a negative status of class REFUSED/USAGE/SYSTEM to its error type. */
export function errorFor(status: number, cls: number, name: string, detail: string): ZxvError | ZxvFatalError {
  if (cls === StatusClass.FATAL) return new ZxvFatalError(status, name, detail);
  if (cls === StatusClass.USAGE) {
    if (status === Status.E_FLOAT) return new FloatRejectedError(detail);
    return new ZxvUsageError(status, name, detail);
  }
  if (cls === StatusClass.REFUSED) {
    switch (status) {
      case Status.E_CAP: return new CapabilityDeniedError(status, name, detail);
      case Status.E_BUDGET: return new BudgetExhaustedError(status, name, detail);
      case Status.E_FUNDS: return new InsufficientFundsError(status, name, detail);
      case Status.E_VERIFY: return new VerificationFailedError(status, name, detail);
      case Status.E_NOTIMPL: return new NotImplementedError(status, name, detail);
      default: return new ZxvRefusedError(status, name, detail);
    }
  }
  return new ZxvSystemError(status, name, detail);
}

/** Apply a fatal policy: abort the process, or return the error to throw. */
export function fatal(policy: FatalPolicy, err: ZxvFatalError): ZxvFatalError {
  if (policy === 'abort') {
    const proc = (globalThis as { process?: { abort?: () => never; stderr?: { write(s: string): void } } }).process;
    if (proc?.abort) {
      proc.stderr?.write(`zxv: ${err.message}; fatalPolicy 'abort': aborting\n`);
      proc.abort();
    }
  }
  return err;
}

/** A USAGE error raised by the SDK itself (before any C call). */
export function usage(detail: string, status: number = Status.E_ARG): ZxvUsageError {
  const names: Record<number, string> = {
    [Status.E_ARG]: 'ZXV_E_ARG',
    [Status.E_RANGE]: 'ZXV_E_RANGE',
    [Status.E_FORMAT]: 'ZXV_E_FORMAT',
  };
  return new ZxvUsageError(status, names[status] ?? 'ZXV_E_ARG', detail);
}
