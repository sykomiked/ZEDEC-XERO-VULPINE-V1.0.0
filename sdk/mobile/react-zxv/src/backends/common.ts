// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Shared by the in-process backends (z-web, @zedec/zxv): error mapping and
// the duplicate-reference resolution that makes a ledger post idempotent.

import { E_DUPLICATE, IdempotencyConflictError, type PostOutcome, type PostRequest, type StatusClassName, ZxvCallError } from '../backend.js';

/** A journal line as the SDKs report it. */
export interface JournalLine {
  readonly entryId: bigint;
  readonly amount: bigint;
  readonly counterparty: number;
  readonly reference: string;
}

const CLASSES = new Set<string>(['USAGE', 'REFUSED', 'FATAL', 'SYSTEM']);
const BY_NAME: Record<string, StatusClassName> = {
  ZxvUsageError: 'USAGE', ZxvRefusedError: 'REFUSED', ZxvSystemError: 'SYSTEM', ZxvFatalError: 'FATAL',
};

/** The status class of an SDK error: its own field, or its class hierarchy. */
function classOf(e: object): StatusClassName | undefined {
  const own = (e as { statusClass?: unknown }).statusClass;
  if (typeof own === 'string' && CLASSES.has(own)) return own as StatusClassName;
  for (let p = Object.getPrototypeOf(e) as object | null; p; p = Object.getPrototypeOf(p) as object | null) {
    const n = (p as { constructor?: { name?: string } }).constructor?.name;
    if (n && BY_NAME[n]) return BY_NAME[n];
  }
  return undefined;
}

/** Map an SDK / z-web error with a libzxv status to a ZxvCallError; anything else is returned unchanged. */
export function mapError(e: unknown): unknown {
  if (typeof e !== 'object' || e === null || typeof (e as { status?: unknown }).status !== 'number') return e;
  const x = e as { status: number; statusName?: unknown; kind?: unknown; name?: unknown; message?: unknown };
  const init: ConstructorParameters<typeof ZxvCallError>[1] = {
    status: x.status,
    kind: typeof x.kind === 'string' ? x.kind : typeof x.name === 'string' ? x.name : 'Error',
  };
  if (typeof x.statusName === 'string') init.statusName = x.statusName;
  const cls = classOf(e);
  if (cls) init.statusClass = cls;
  return new ZxvCallError(typeof x.message === 'string' ? x.message : String(e), init);
}

/**
 * Post with the key as the ledger reference. If the ledger says the
 * reference is taken, find that posting in the sender's journal and return
 * it as `replayed` (or refuse when it is a different transfer).
 */
export async function postOnce(
  post: () => Promise<bigint>, journal: () => Promise<readonly JournalLine[]>, req: PostRequest,
): Promise<PostOutcome> {
  try {
    return { entryId: (await post()).toString(), replayed: false };
  } catch (raw) {
    const e = mapError(raw);
    if (!(e instanceof ZxvCallError) || e.status !== E_DUPLICATE) throw e;
    const hit = (await journal()).find((j) => j.reference === req.idempotencyKey);
    if (!hit) throw e;
    if (hit.counterparty !== req.to || hit.amount !== -BigInt(req.amountMinor)) throw new IdempotencyConflictError(req.idempotencyKey);
    return { entryId: hit.entryId.toString(), replayed: true };
  }
}
