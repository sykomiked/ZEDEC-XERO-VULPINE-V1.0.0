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
  /** Not a face of the Fibonacci coin or token ladder (ABI 1.5; class USAGE). */
  E_DENOM: -32,
  /** A treaty fails validation; the fault and offset say why (ABI 1.5; class USAGE). */
  E_TREATY: -33,
  /** Content does not match its CID, or the CID bytes are bad (ABI 1.5; class REFUSED). */
  E_CID: -34,
  /** No valid consent token for this agent action (ABI 1.5; class REFUSED). */
  E_CONSENT: -35,
  /** Too few freight rows arrived to rebuild the payload (ABI 1.5; class REFUSED). */
  E_ERASURE: -36,
  /** No valid signed peg for this currency (unset, stale or replayed epoch) (ABI 1.5; class REFUSED). */
  E_PEG: -37,
  /** A V-Bill hub limit rule refused the swap; not a fault (ABI 1.5; class REFUSED). */
  E_LIMIT: -38,
  /** A paradox graph reached its step limit and goes to the owner (ABI 1.5; class REFUSED). */
  E_PARADOX_STEP: -39,
  /** The change touches money or devices and needs the owner's approval (ABI 1.5; class REFUSED). */
  E_PARADOX_OWNER: -40,
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

/**
 * `ZXV_E_DENOM` (ABI 1.5, class USAGE): the amount is not one of the coin
 * ladder's faces (1, 2, 3, 5, 8, 13, ...) or of the 64 token tiers. Split an
 * arbitrary amount with `zxv.coins.split` instead.
 */
export class DenominationError extends ZxvUsageError {}

/**
 * `ZXV_E_TREATY` (ABI 1.5, class USAGE): a treaty broke a validation rule.
 * {@link TreatyError.fault} is the `TreatyFault` reason and
 * {@link TreatyError.offset} the byte offset of the first fault in the
 * parsed text or record; both are `undefined` when the call that failed
 * does not report them (emitting, running).
 */
export class TreatyError extends ZxvUsageError {
  /** The `TreatyFault` value, when the call reported one. */
  readonly fault: number | undefined;
  /** Byte offset of the first fault in the parsed input, when the call reported one. */
  readonly offset: number | undefined;

  /**
   * @param status - always `ZXV_E_TREATY` (-33)
   * @param statusName - "ZXV_E_TREATY"
   * @param detail - zxv_last_error text (diagnostic only)
   * @param fault - the `TreatyFault` reason, if known
   * @param offset - byte offset of the first fault, if known
   */
  constructor(status: number, statusName: string, detail: string, fault?: number, offset?: number) {
    super(status, statusName, detail);
    this.fault = fault;
    this.offset = offset;
  }
}

/** `ZXV_E_CID` (ABI 1.5): bytes do not match their CID, or the CID itself is malformed. */
export class CidMismatchError extends ZxvRefusedError {}
/** `ZXV_E_CONSENT` (ABI 1.5): no valid, unused consent token covers this agent action. */
export class ConsentDeniedError extends ZxvRefusedError {}
/** `ZXV_E_ERASURE` (ABI 1.5): fewer freight rows than the code needs; wait for more rows and decode again. */
export class ErasureError extends ZxvRefusedError {}
/** `ZXV_E_PEG` (ABI 1.5): the currency has no valid signed peg (unset, or a stale or replayed epoch). */
export class PegError extends ZxvRefusedError {}
/** `ZXV_E_LIMIT` (ABI 1.5): a hub limit rule refused the swap. A decision, not a fault. */
export class LimitExceededError extends ZxvRefusedError {}
/** `ZXV_E_PARADOX_STEP` (ABI 1.5): a paradox graph reached its step limit; it now goes to the owner. */
export class ParadoxStepLimitError extends ZxvRefusedError {}
/** `ZXV_E_PARADOX_OWNER` (ABI 1.5): the change touches money or devices, so the owner must approve it. */
export class OwnerApprovalRequiredError extends ZxvRefusedError {}

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
    if (status === Status.E_DENOM) return new DenominationError(status, name, detail);
    if (status === Status.E_TREATY) return new TreatyError(status, name, detail);
    return new ZxvUsageError(status, name, detail);
  }
  if (cls === StatusClass.REFUSED) {
    switch (status) {
      case Status.E_CAP: return new CapabilityDeniedError(status, name, detail);
      case Status.E_BUDGET: return new BudgetExhaustedError(status, name, detail);
      case Status.E_FUNDS: return new InsufficientFundsError(status, name, detail);
      case Status.E_VERIFY: return new VerificationFailedError(status, name, detail);
      case Status.E_NOTIMPL: return new NotImplementedError(status, name, detail);
      case Status.E_CID: return new CidMismatchError(status, name, detail);
      case Status.E_CONSENT: return new ConsentDeniedError(status, name, detail);
      case Status.E_ERASURE: return new ErasureError(status, name, detail);
      case Status.E_PEG: return new PegError(status, name, detail);
      case Status.E_LIMIT: return new LimitExceededError(status, name, detail);
      case Status.E_PARADOX_STEP: return new ParadoxStepLimitError(status, name, detail);
      case Status.E_PARADOX_OWNER: return new OwnerApprovalRequiredError(status, name, detail);
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
    [Status.E_TOO_LONG]: 'ZXV_E_TOO_LONG',
  };
  return new ZxvUsageError(status, names[status] ?? 'ZXV_E_ARG', detail);
}
