// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A backend on @zedec/zxv in the same process: the Node-API addon (server
// rendering, Electron, tests) or its wasm build. The types are the parts of
// the SDK's Ledger, Session and Budget it calls; pass objects that already
// hold their capabilities (Ledger.of(cap), Budgets.open(cap).create(...),
// Model.open(cap, ...).newSession()).

import { type Feature, Reachability, throwIfAborted, type ZxvBackend } from '../backend.js';
import { mapError, postOnce } from './common.js';

interface Exact {
  readonly num: bigint;
  readonly den: bigint;
}

/** The parts of the SDK's `Ledger` used. */
export interface SdkLedgerLike {
  post(from: number, to: number, amount: bigint, currency: string, reference: string): bigint;
  balance(account: number): { debit: bigint; credit: bigint; equity: bigint };
  journal(account: number): readonly { entryId: bigint; amount: bigint; counterparty: number; reference: string }[];
}

/** The parts of the SDK's `Session` used (sampling values are exact text, which Rational.parse reads). */
export interface SdkSessionLike {
  generate(prompt: string, options: {
    maxTokens?: number; temperature?: string; topK?: number; topP?: string; seed?: bigint; signal?: AbortSignal;
    onToken?: (t: { id: number; text: string }) => boolean | void;
  }): Promise<{ text: string; tokens: readonly number[]; stop: number }>;
}

/** The parts of the SDK's `Budget` used. */
export interface SdkBudgetLike {
  consume(amount: string): void;
  release(amount: string): void;
  status(): { remaining: Exact; used: Exact };
}

/** What the SDK backend uses. Leave out what the app does not use. */
export interface SdkBackendOptions {
  ledger?: SdkLedgerLike;
  session?: SdkSessionLike;
  budgets?: Readonly<Record<string, SdkBudgetLike>>;
}

/** `ZXV_STOP_*` names (the SDK's StopReason values). */
const STOP: Record<number, string> = { 1: 'EOS', 2: 'MAX_TOKENS', 3: 'STRING', 4: 'CANCELLED', 5: 'CONTEXT_FULL', 6: 'BUDGET' };
const rat = (r: Exact): string => `${r.num}/${r.den}`;

/** The in-process SDK backend (always reachable). */
export function createSdkBackend(o: SdkBackendOptions): ZxvBackend {
  const features = new Set<Feature>();
  if (o.session) features.add('generate').add('stream').add('cancel');
  if (o.budgets) features.add('budget');
  if (o.ledger) features.add('ledger');
  const reach = new Reachability(true);
  const budget = (name: string): SdkBudgetLike => {
    const b = o.budgets?.[name];
    if (!b) throw new RangeError(`no budget named ${JSON.stringify(name)}`);
    return b;
  };
  async function guard<T>(signal: AbortSignal, f: () => T | Promise<T>): Promise<T> {
    throwIfAborted(signal);
    try {
      return await f();
    } catch (e) {
      throw mapError(e);
    }
  }
  return {
    name: 'zxv-sdk',
    features,
    isReachable: () => true,
    subscribe: (l) => reach.subscribe(l),
    generate: (req, onChunk, signal) => guard(signal, async () => {
      const opts: Parameters<SdkSessionLike['generate']>[1] = { signal, onToken: (t) => { onChunk({ id: t.id, text: t.text }); } };
      if (req.maxTokens !== undefined) opts.maxTokens = req.maxTokens;
      if (req.temperature !== undefined) opts.temperature = req.temperature;
      if (req.topK !== undefined) opts.topK = req.topK;
      if (req.topP !== undefined) opts.topP = req.topP;
      if (req.seed !== undefined) opts.seed = BigInt(req.seed);
      const r = await o.session!.generate(req.prompt, opts);
      return { text: r.text, stop: STOP[r.stop] ?? String(r.stop), tokens: r.tokens };
    }),
    budgetStatus: (name, signal) => guard(signal, () => {
      const s = budget(name).status();
      return { remaining: rat(s.remaining), used: rat(s.used) };
    }),
    budgetConsume: (name, amount, signal) => guard(signal, () => budget(name).consume(amount)),
    budgetRelease: (name, amount, signal) => guard(signal, () => budget(name).release(amount)),
    ledgerBalance: (account, signal) => guard(signal, () => {
      const b = o.ledger!.balance(account);
      return { debit: b.debit.toString(), credit: b.credit.toString(), equity: b.equity.toString() };
    }),
    ledgerPost: (req, signal) => guard(signal, () => postOnce(
      async () => o.ledger!.post(req.from, req.to, BigInt(req.amountMinor), req.currency, req.idempotencyKey),
      async () => o.ledger!.journal(req.from),
      req,
    )),
  };
}
