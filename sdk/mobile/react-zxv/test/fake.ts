// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A fake backend with a real-shaped ledger (unique references, duplicate =
// ZXV_E_DUPLICATE, resolved by the same postOnce the real adapters use),
// exact budgets, a scripted token stream, and switches for going offline
// and for losing an answer after the work was done.

import { BackendUnavailableError, type Feature, Reachability, ZxvCallError, type ZxvBackend } from '../src/index.js';
import { postOnce } from '../src/backends/common.js';
import { ExactRational } from '../src/rational.js';

export interface Posting {
  entryId: bigint;
  from: number;
  to: number;
  amount: bigint;
  currency: string;
  reference: string;
}

const tick = (ms = 0): Promise<void> => new Promise((ok) => setTimeout(ok, ms));

export class FakeBackend implements ZxvBackend {
  readonly name = 'fake';
  features: Set<Feature> = new Set<Feature>(['generate', 'stream', 'cancel', 'budget', 'ledger']);
  readonly reach = new Reachability(true);
  /** Every call that reached the backend, in order. */
  readonly calls: string[] = [];
  readonly postings: Posting[] = [];
  readonly opening = new Map<number, bigint>();
  readonly budgets = new Map<string, { limit: ExactRational; used: ExactRational }>();
  /** The next N posts are committed, then the answer is lost ("maybe delivered"). */
  loseAnswerAfterCommit = 0;
  /** The next N budget consumes are applied, then the answer is lost. */
  loseConsumeAnswer = 0;
  /** Tokens the generator streams, and the delay between them. */
  script = ['Hel', 'lo', ', ', 'wor', 'ld'];
  tokenDelayMs = 5;
  /** Signals seen by generate (to check that cancel reached the backend). */
  readonly genSignals: AbortSignal[] = [];
  #entry = 100n;

  isReachable = (): boolean => this.reach.get();
  subscribe = (l: (r: boolean) => void): (() => void) => this.reach.subscribe(l);
  setReachable(r: boolean): void {
    this.reach.set(r);
  }

  #gate(what: string): void {
    if (!this.reach.get()) throw new BackendUnavailableError(`fake: offline (${what})`, 'no');
    this.calls.push(what);
  }

  balanceOf(account: number): { debit: bigint; credit: bigint } {
    let debit = this.opening.get(account) ?? 0n;
    let credit = 0n;
    for (const p of this.postings) {
      if (p.to === account) debit += p.amount;
      if (p.from === account) credit += p.amount;
    }
    return { debit, credit };
  }

  async generate(req: { prompt: string; maxTokens?: number }, onChunk: (c: { text: string; id?: number }) => void, signal: AbortSignal) {
    this.#gate(`generate:${req.prompt}`);
    this.genSignals.push(signal);
    let text = '';
    const n = Math.min(this.script.length, req.maxTokens ?? this.script.length);
    for (let i = 0; i < n; i++) {
      await tick(this.tokenDelayMs);
      if (signal.aborted) return { text, stop: 'CANCELLED' };
      if (!this.reach.get()) throw new BackendUnavailableError('fake: dropped mid-generation', 'maybe');
      text += this.script[i]!;
      onChunk({ id: i, text: this.script[i]! });
    }
    return { text, stop: n < this.script.length ? 'MAX_TOKENS' : 'EOS' };
  }

  #budget(name: string) {
    const b = this.budgets.get(name);
    if (!b) throw new ZxvCallError(`no budget ${name}`, { status: -4, statusName: 'ZXV_E_NOT_FOUND', statusClass: 'REFUSED' });
    return b;
  }

  async budgetStatus(name: string) {
    this.#gate(`budgetStatus:${name}`);
    const b = this.#budget(name);
    return { remaining: b.limit.sub(b.used).toWire(), used: b.used.toWire() };
  }

  async budgetConsume(name: string, amount: string) {
    this.#gate(`budgetConsume:${name}:${amount}`);
    const b = this.#budget(name);
    const a = ExactRational.fromWire(amount);
    if (b.used.add(a).cmp(b.limit) > 0) {
      throw new ZxvCallError('ZXV_E_BUDGET', { kind: 'BudgetExhaustedError', status: -19, statusName: 'ZXV_E_BUDGET', statusClass: 'REFUSED' });
    }
    b.used = b.used.add(a);
    if (this.loseConsumeAnswer > 0) {
      this.loseConsumeAnswer--;
      throw new BackendUnavailableError('fake: answer lost', 'maybe');
    }
  }

  async budgetRelease(name: string, amount: string) {
    this.#gate(`budgetRelease:${name}:${amount}`);
    const b = this.#budget(name);
    b.used = b.used.sub(ExactRational.fromWire(amount));
  }

  async ledgerBalance(account: number) {
    this.#gate(`ledgerBalance:${account}`);
    const { debit, credit } = this.balanceOf(account);
    return { debit: debit.toString(), credit: credit.toString(), equity: (debit - credit).toString() };
  }

  async ledgerPost(req: { from: number; to: number; amountMinor: string; currency: string; idempotencyKey: string }) {
    this.#gate(`ledgerPost:${req.idempotencyKey}`);
    const out = await postOnce(
      async () => {
        if (this.postings.some((p) => p.reference === req.idempotencyKey)) {
          throw Object.assign(new Error('ZXV_E_DUPLICATE'), { status: -13, statusName: 'ZXV_E_DUPLICATE', statusClass: 'USAGE' });
        }
        const amount = BigInt(req.amountMinor);
        const { debit, credit } = this.balanceOf(req.from);
        if (debit - credit < amount) {
          throw Object.assign(new Error('ZXV_E_FUNDS'), { status: -6, statusName: 'ZXV_E_FUNDS', statusClass: 'REFUSED' });
        }
        const entryId = this.#entry++;
        this.postings.push({ entryId, from: req.from, to: req.to, amount, currency: req.currency, reference: req.idempotencyKey });
        return entryId;
      },
      async () => this.postings.filter((p) => p.from === req.from).map((p) => ({
        entryId: p.entryId, amount: -p.amount, counterparty: p.to, reference: p.reference,
      })),
      req,
    );
    if (!out.replayed && this.loseAnswerAfterCommit > 0) {
      this.loseAnswerAfterCommit--;
      throw new BackendUnavailableError('fake: committed, answer lost', 'maybe');
    }
    return out;
  }
}

/** An in-memory QueueStorage. */
export class MemoryStorage {
  data: string | null = null;
  saves = 0;
  async load(): Promise<string | null> {
    return this.data;
  }
  async save(d: string): Promise<void> {
    this.saves++;
    this.data = d;
  }
}

export { tick };
