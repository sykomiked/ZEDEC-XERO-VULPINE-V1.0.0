// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The React layer: ZxvProvider and the hooks. They work the same in React
// DOM and React Native; only the backend given to the provider differs.

import {
  createContext, createElement, type ReactNode, useCallback, useContext, useEffect, useMemo, useRef, useState, useSyncExternalStore,
} from 'react';

import type { Feature, GenerateOutcome, GenerateRequest, ZxvBackend } from './backend.js';
import { type Balance, type BudgetState, type ClientOptions, type ClientState, type Posted, type Transfer, ZxvClient } from './client.js';
import { ExactRational, intToWire, type RationalInput } from './rational.js';

const Ctx = createContext<ZxvClient | null>(null);

/** Props of {@link ZxvProvider}: a backend (a client is made for it) or a ready client. */
export type ZxvProviderProps = ({ backend: ZxvBackend; options?: ClientOptions; client?: never } | { client: ZxvClient; backend?: never })
  & { children?: ReactNode };

/** Makes a {@link ZxvClient} for the backend (once) and gives it to the hooks below. */
export function ZxvProvider(props: ZxvProviderProps): ReturnType<typeof createElement> {
  const { backend, client: given, children } = props;
  const options = 'options' in props ? props.options : undefined;
  const [made] = useState(() => given ?? new ZxvClient(backend!, options));
  const client = given ?? made;
  useEffect(() => client.attach(), [client]);
  return createElement(Ctx.Provider, { value: client }, children);
}

/** What {@link useZxv} returns. */
export interface UseZxv extends ClientState {
  readonly client: ZxvClient;
  readonly features: ReadonlySet<Feature>;
  /** Replay queued calls now. */
  retry(): Promise<void>;
}

function useClient(): ZxvClient {
  const c = useContext(Ctx);
  if (!c) throw new Error('ZXV hooks must be used inside <ZxvProvider>');
  return c;
}

/** The client and its live state (online, pending queue length, replaying). */
export function useZxv(): UseZxv {
  const client = useClient();
  const state = useSyncExternalStore(client.subscribe, client.getState, client.getState);
  return useMemo(() => ({ ...state, client, features: client.backend.features, retry: () => client.retry() }), [state, client]);
}

function useMounted(): { readonly current: boolean } {
  const m = useRef(true);
  useEffect(() => {
    m.current = true;
    return () => {
      m.current = false;
    };
  }, []);
  return m;
}

// ---- useGenerate -----------------------------------------------------------

/** Exact sampling options. */
export interface GenerateOptions {
  maxTokens?: number;
  temperature?: RationalInput;
  topK?: number;
  topP?: RationalInput;
  seed?: bigint | number;
}

/** Where a generation is. */
export type GenerateStatus = 'idle' | 'queued' | 'running' | 'done' | 'cancelled' | 'error';

/** What {@link useGenerate} returns. */
export interface UseGenerate {
  readonly status: GenerateStatus;
  /** The output so far (streamed), then the final text. */
  readonly text: string;
  /** Chunks received (tokens, for a streaming backend). */
  readonly chunks: number;
  readonly stop: string | null;
  readonly error: unknown;
  /** Start (cancels a running one). Resolves with the outcome, or undefined if cancelled or failed. */
  generate(prompt: string, options?: GenerateOptions): Promise<GenerateOutcome | undefined>;
  /** Stop the current generation (also on unmount). */
  cancel(): void;
}

interface GenState {
  status: GenerateStatus;
  text: string;
  chunks: number;
  stop: string | null;
  error: unknown;
}
const IDLE: GenState = { status: 'idle', text: '', chunks: 0, stop: null, error: null };

/** Streaming generation with cancellation. */
export function useGenerate(): UseGenerate {
  const client = useClient();
  const mounted = useMounted();
  const [s, set] = useState<GenState>(IDLE);
  const current = useRef<AbortController | null>(null);

  const cancel = useCallback(() => current.current?.abort(), []);
  useEffect(() => () => current.current?.abort(), []);

  const generate = useCallback(async (prompt: string, options: GenerateOptions = {}): Promise<GenerateOutcome | undefined> => {
    current.current?.abort();
    const ctl = new AbortController();
    current.current = ctl;
    const mine = (): boolean => mounted.current && current.current === ctl;
    let request: GenerateRequest;
    try {
      request = toRequest(prompt, options);
    } catch (e) {
      if (mine()) set({ ...IDLE, status: 'error', error: e });
      return undefined;
    }
    set({ ...IDLE, status: 'running' });
    try {
      const out = await client.generate(request, (c) => {
        if (mine()) set((p) => ({ ...p, status: 'running', text: p.text + c.text, chunks: p.chunks + 1 }));
      }, { signal: ctl.signal, onQueued: () => { if (mine()) set((p) => ({ ...p, status: 'queued' })); } });
      if (mine()) set((p) => ({ ...p, status: out.stop === 'CANCELLED' ? 'cancelled' : 'done', text: out.text, stop: out.stop }));
      return out;
    } catch (e) {
      if (mine()) set((p) => (ctl.signal.aborted ? { ...p, status: 'cancelled', stop: 'CANCELLED' } : { ...p, status: 'error', error: e }));
      return undefined;
    } finally {
      if (current.current === ctl) current.current = null;
    }
  }, [client, mounted]);

  return useMemo(() => ({ ...s, generate, cancel }), [s, generate, cancel]);
}

function toRequest(prompt: string, o: GenerateOptions): GenerateRequest {
  const r: { -readonly [K in keyof GenerateRequest]: GenerateRequest[K] } = { prompt };
  if (o.maxTokens !== undefined) r.maxTokens = o.maxTokens;
  if (o.topK !== undefined) r.topK = o.topK;
  if (o.temperature !== undefined) r.temperature = ExactRational.from(o.temperature, 'temperature').toWire();
  if (o.topP !== undefined) r.topP = ExactRational.from(o.topP, 'topP').toWire();
  if (o.seed !== undefined) r.seed = intToWire(o.seed, 'seed');
  return r;
}

// ---- useBudget -------------------------------------------------------------

/** What {@link useBudget} returns. */
export interface UseBudget {
  /** Exact remaining and used, once loaded. */
  readonly state: BudgetState | null;
  readonly loading: boolean;
  readonly error: unknown;
  /** Consume exactly `amount` (floats are refused), then refresh. Rejects on refusal. */
  consume(amount: RationalInput): Promise<void>;
  release(amount: RationalInput): Promise<void>;
  refresh(): Promise<void>;
}

/** One budget, by the name the backend knows it by. */
export function useBudget(budget: string): UseBudget {
  const client = useClient();
  const mounted = useMounted();
  const [s, set] = useState<{ state: BudgetState | null; loading: boolean; error: unknown }>({ state: null, loading: true, error: null });
  const seq = useRef(0);

  const refresh = useCallback(async () => {
    const n = ++seq.current;
    set((p) => ({ ...p, loading: true }));
    try {
      const st = await client.budgetStatus(budget);
      if (mounted.current && n === seq.current) set({ state: st, loading: false, error: null });
    } catch (e) {
      if (mounted.current && n === seq.current) set((p) => ({ ...p, loading: false, error: e }));
    }
  }, [client, budget, mounted]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const change = useCallback(async (op: 'budgetConsume' | 'budgetRelease', amount: RationalInput) => {
    try {
      await client[op](budget, amount);
    } catch (e) {
      await refresh();
      if (mounted.current) set((p) => ({ ...p, error: e })); // the refusal stays visible after the refresh
      throw e;
    }
    await refresh();
  }, [client, budget, mounted, refresh]);

  const consume = useCallback((a: RationalInput) => change('budgetConsume', a), [change]);
  const release = useCallback((a: RationalInput) => change('budgetRelease', a), [change]);
  return useMemo(() => ({ ...s, consume, release, refresh }), [s, consume, release, refresh]);
}

// ---- useRational -----------------------------------------------------------

/** What {@link useRational} returns. */
export interface UseRational {
  /** The exact value, or null if the input is not exact. */
  readonly value: ExactRational | null;
  readonly error: Error | null;
  /** "num/den", or null. */
  readonly wire: string | null;
  /** Exact text ("12.75", "1/3"), or null. */
  readonly text: string | null;
  /** A rounded decimal for display only. */
  display(fractionDigits?: number): string | null;
}

/** Parse and hold an exact value (e.g. from a text input); a float is an error, never a value. */
export function useRational(input: RationalInput | null | undefined): UseRational {
  return useMemo(() => {
    if (input === null || input === undefined || input === '') {
      return { value: null, error: null, wire: null, text: null, display: () => null };
    }
    try {
      const v = ExactRational.from(input);
      return { value: v, error: null, wire: v.toWire(), text: v.toString(), display: (d = 2) => v.toDisplay(d) };
    } catch (e) {
      return { value: null, error: e as Error, wire: null, text: null, display: () => null };
    }
  }, [input]);
}

// ---- useLedgerBalance ------------------------------------------------------

/** Where a balance is. */
export type BalanceStatus = 'loading' | 'queued' | 'ready' | 'error';

/** What {@link useLedgerBalance} returns. */
export interface UseLedgerBalance {
  readonly balance: Balance | null;
  readonly status: BalanceStatus;
  readonly error: unknown;
  refresh(): Promise<void>;
  /**
   * Post from this account, exactly once (offline: queued, replayed later).
   * Refreshes the balance when it lands.
   */
  post(transfer: Omit<Transfer, 'from'>): Promise<Posted>;
}

/** An account's balances, kept fresh across posts and reconnections. */
export function useLedgerBalance(account: number): UseLedgerBalance {
  const client = useClient();
  const mounted = useMounted();
  const [s, set] = useState<{ balance: Balance | null; status: BalanceStatus; error: unknown }>({ balance: null, status: 'loading', error: null });
  const seq = useRef(0);

  const refresh = useCallback(async () => {
    const n = ++seq.current;
    const mine = (): boolean => mounted.current && n === seq.current;
    set((p) => ({ ...p, status: p.balance ? p.status : 'loading' }));
    try {
      const b = await client.ledgerBalance(account, { onQueued: () => { if (mine()) set((p) => ({ ...p, status: 'queued' })); } });
      if (mine()) set({ balance: b, status: 'ready', error: null });
    } catch (e) {
      if (mine()) set((p) => ({ ...p, status: 'error', error: e }));
    }
  }, [client, account, mounted]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // posts replayed after a restart can move this balance
  useEffect(() => client.onReplay(() => void refresh()), [client, refresh]);

  const post = useCallback(async (t: Omit<Transfer, 'from'>) => {
    try {
      return await client.ledgerPost({ ...t, from: account });
    } finally {
      if (mounted.current) await refresh();
    }
  }, [client, account, mounted, refresh]);

  return useMemo(() => ({ ...s, refresh, post }), [s, refresh, post]);
}
