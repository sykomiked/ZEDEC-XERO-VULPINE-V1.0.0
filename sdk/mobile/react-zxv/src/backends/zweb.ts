// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A backend on @zedec/zxv-web (the wasm build in a Web Worker). The types
// below are the parts of ZxvWebClient it uses, so this package does not
// depend on z-web; pass the client and the handles it should present.

import { BackendUnavailableError, type Feature, Reachability, type ZxvBackend } from '../backend.js';
import { mapError, postOnce } from './common.js';

interface Exact {
  readonly num: bigint;
  readonly den: bigint;
}
interface CallOpts {
  signal?: AbortSignal;
}

/** The parts of `ZxvWebClient` the backend uses. */
export interface ZWebClientLike<Cap, Session, Budget> {
  readonly closed: boolean;
  generate(session: Session, prompt: string, options: {
    maxTokens?: number; temperature?: string; topK?: number; topP?: string; seed?: bigint; signal?: AbortSignal;
    onToken?: (t: { id: number; text: string }) => void;
  }): Promise<{ text: string; tokens: readonly number[]; stop: string }>;
  budgetStatus(budget: Budget, o?: CallOpts): Promise<{ remaining: Exact; used: Exact }>;
  consume(budget: Budget, amount: string, o?: CallOpts): Promise<void>;
  release(budget: Budget, amount: string, o?: CallOpts): Promise<void>;
  balance(cap: Cap, account: number, o?: CallOpts): Promise<{ debit: bigint; credit: bigint; equity: bigint }>;
  post(cap: Cap, t: { from: number; to: number; amount: bigint; currency: string; reference: string }, o?: CallOpts): Promise<bigint>;
  journal(cap: Cap, account: number, o?: CallOpts): Promise<{ entryId: bigint; amount: bigint; counterparty: number; reference: string }[]>;
}

/** What the z-web backend presents. Leave out what the app does not use: that feature is then off. */
export interface ZWebBackendOptions<Cap, Session, Budget> {
  client: ZWebClientLike<Cap, Session, Budget>;
  /** The capability for ledger calls (LEDGER_READ for balances, LEDGER_POST to post). */
  ledgerCap?: Cap;
  /** The session generations run in. */
  session?: Session;
  /** Budgets by the names the hooks use. */
  budgets?: Readonly<Record<string, Budget>>;
}

const rat = (r: Exact): string => `${r.num}/${r.den}`;

/** The z-web backend. Reachable until the worker dies. */
export function createZWebBackend<Cap, Session, Budget>(o: ZWebBackendOptions<Cap, Session, Budget>): ZxvBackend {
  const { client } = o;
  const reach = new Reachability(!client.closed);
  const features = new Set<Feature>();
  if (o.session !== undefined) features.add('generate').add('stream').add('cancel');
  if (o.budgets) features.add('budget');
  if (o.ledgerCap !== undefined) features.add('ledger');

  async function guard<T>(f: () => Promise<T>): Promise<T> {
    if (client.closed) {
      reach.set(false);
      throw new BackendUnavailableError('the zxv worker is gone', 'no');
    }
    try {
      return await f();
    } catch (e) {
      if ((e as { name?: unknown })?.name === 'WorkerGoneError') {
        reach.set(false);
        throw new BackendUnavailableError('the zxv worker is gone', 'maybe');
      }
      throw mapError(e);
    }
  }
  const budget = (name: string): Budget => {
    const b = o.budgets?.[name];
    if (b === undefined) throw new RangeError(`no budget named ${JSON.stringify(name)}`);
    return b;
  };

  return {
    name: 'z-web',
    features,
    isReachable: () => reach.get() && !client.closed,
    subscribe: (l) => reach.subscribe(l),
    generate: (req, onChunk, signal) => guard(async () => {
      const opts: Parameters<typeof client.generate>[2] = { signal, onToken: (t) => onChunk({ id: t.id, text: t.text }) };
      if (req.maxTokens !== undefined) opts.maxTokens = req.maxTokens;
      if (req.temperature !== undefined) opts.temperature = req.temperature;
      if (req.topK !== undefined) opts.topK = req.topK;
      if (req.topP !== undefined) opts.topP = req.topP;
      if (req.seed !== undefined) opts.seed = BigInt(req.seed);
      const r = await client.generate(o.session as Session, req.prompt, opts);
      return { text: r.text, stop: r.stop, tokens: r.tokens };
    }),
    budgetStatus: (name, signal) => guard(async () => {
      const s = await client.budgetStatus(budget(name), { signal });
      return { remaining: rat(s.remaining), used: rat(s.used) };
    }),
    budgetConsume: (name, amount, signal) => guard(() => client.consume(budget(name), amount, { signal })),
    budgetRelease: (name, amount, signal) => guard(() => client.release(budget(name), amount, { signal })),
    ledgerBalance: (account, signal) => guard(async () => {
      const b = await client.balance(o.ledgerCap as Cap, account, { signal });
      return { debit: b.debit.toString(), credit: b.credit.toString(), equity: b.equity.toString() };
    }),
    ledgerPost: (req, signal) => guard(() => postOnce(
      () => client.post(o.ledgerCap as Cap, {
        from: req.from, to: req.to, amount: BigInt(req.amountMinor), currency: req.currency, reference: req.idempotencyKey,
      }, { signal }),
      () => client.journal(o.ledgerCap as Cap, req.from, { signal }),
      req,
    )),
  };
}
