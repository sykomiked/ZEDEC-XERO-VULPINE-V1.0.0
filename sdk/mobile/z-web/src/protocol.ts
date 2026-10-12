// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The typed message protocol between a page and the zxv worker
// ("zxv-web/1"). Every request carries a numeric id; the worker answers
// with exactly one `result` or `error` for that id, preceded by any number
// of `token` events for a generation. A `cancel` request has no answer.
//
// Objects in the worker (contexts, capabilities, budgets, models,
// sessions) are named by numeric handles. A capability is never implied:
// every gated request names the capability handle it presents.
//
// Exact values travel as strings (see wire.ts): rationals as "num/den",
// 64-bit integers as decimal strings. Every other number is a safe integer.

import type { WireError } from './errors.js';
import type { WireInt, WireRational } from './wire.js';

/** Protocol name and version, carried in every message. */
export const PROTOCOL = 'zxv-web/1';

/** A numeric handle naming one object in the worker. */
export type Handle = number;

/** Truth names, kernel order 0..3. */
export type WireTruth = 'NEITHER' | 'TRUE' | 'FALSE' | 'BOTH';

/** Why a generation stopped. */
export type WireStop = 'EOS' | 'MAX_TOKENS' | 'STRING' | 'CANCELLED' | 'CONTEXT_FULL' | 'BUDGET';

/** Sampling options, exact. */
export interface WireGenerateOptions {
  maxTokens?: number;
  temperature?: WireRational;
  topK?: number;
  topP?: WireRational;
  repeatPenalty?: WireRational;
  repeatLastN?: number;
  seed?: WireInt;
}

/** Every request, by `op`, with its result type. */
export interface Ops {
  /** Load the wasm module (bytes the page already checked). */
  init: { req: { wasm: ArrayBuffer }; res: { major: number; minor: number; patch: number; backend: string } };
  /** A new context and its root capability. `caps` limits the root (default every 1.4 bit). */
  'context.create': { req: { caps?: WireInt }; res: { context: Handle; root: Handle; caps: WireInt } };
  'cap.attenuate': { req: { cap: Handle; keep: WireInt }; res: { cap: Handle; caps: WireInt } };
  'cap.revoke': { req: { cap: Handle }; res: null };
  'cap.setAttestation': { req: { cap: Handle; truth: WireTruth }; res: null };
  'cap.info': { req: { cap: Handle }; res: { caps: WireInt; attestation: WireTruth; revoked: boolean } };
  /** Close any handle (a context closes everything made from it). */
  'handle.close': { req: { handle: Handle }; res: null };
  'currency.enable': { req: { cap: Handle; code: string }; res: null };
  'ledger.openAccount': { req: { cap: Handle; name: string; currency: string; kind: 'holder' | 'issuer' }; res: { account: number } };
  'ledger.post': {
    req: { cap: Handle; from: number; to: number; amountMinor: WireInt; currency: string; reference: string };
    res: { entryId: WireInt };
  };
  'ledger.balance': { req: { cap: Handle; account: number }; res: { debit: WireInt; credit: WireInt; equity: WireInt } };
  'ledger.journal': {
    req: { cap: Handle; account: number };
    res: { entries: { entryId: WireInt; amountMinor: WireInt; counterparty: number; reference: string }[] };
  };
  'budgets.open': { req: { cap: Handle }; res: { budgets: Handle } };
  'budget.create': { req: { budgets: Handle; name: string; limit: WireRational; enforce?: boolean }; res: { budget: Handle } };
  'budget.consume': { req: { budget: Handle; amount: WireRational }; res: null };
  'budget.release': { req: { budget: Handle; amount: WireRational }; res: null };
  'budget.setLimit': { req: { budget: Handle; limit: WireRational }; res: null };
  'budget.status': { req: { budget: Handle }; res: { remaining: WireRational; used: WireRational; epoch: WireInt } };
  'model.load': {
    req: { cap: Handle; model: ArrayBuffer; tokenizer?: ArrayBuffer };
    res: { model: Handle; nVocab: number; nCtxTrain: number; nLayer: number; eos: number };
  };
  'session.create': { req: { model: Handle; nCtx?: number }; res: { session: Handle } };
  'session.reset': { req: { session: Handle }; res: null };
  /** Tokens in the session's context (prompt plus generated). */
  'session.position': { req: { session: Handle }; res: { position: number } };
  'session.bindBudget': { req: { session: Handle; budget: Handle | null }; res: null };
  /**
   * Generate, streaming `token` events. `cancelFlag` (an Int32Array's
   * SharedArrayBuffer, when the page can share memory) lets the page stop
   * the run between two tokens even though the worker is busy in wasm.
   */
  'session.generate': {
    req: { session: Handle; prompt: string; options: WireGenerateOptions; cancelFlag?: SharedArrayBuffer };
    res: { text: string; tokens: number[]; stop: WireStop; promptTokens: number; generatedTokens: number };
  };
  /** libzxv's own SHA3-256 (zxv_sha3_256). */
  'hash.sha3_256': { req: { data: ArrayBuffer }; res: { hex: string } };
}

/** An operation name. */
export type Op = keyof Ops;

/** A request as sent: envelope plus the op's fields. */
export type Request<O extends Op = Op> = { protocol: typeof PROTOCOL; id: number; op: O } & Ops[O]['req'];

/** Stop an earlier request (no reply). */
export interface CancelRequest {
  protocol: typeof PROTOCOL;
  id: number;
  op: 'cancel';
  target: number;
}

/** A generated token. */
export interface WireToken {
  readonly id: number;
  readonly text: string;
}

/** Every message the worker sends. */
export type Reply =
  | { protocol: typeof PROTOCOL; id: number; type: 'result'; result: unknown }
  | { protocol: typeof PROTOCOL; id: number; type: 'error'; error: WireError }
  | { protocol: typeof PROTOCOL; id: number; type: 'token'; token: WireToken };

/** The worker side of a message channel (a DedicatedWorkerGlobalScope, a MessagePort, a shim). */
export interface Endpoint {
  postMessage(message: unknown, transfer?: Transferable[]): void;
  addEventListener(type: 'message', listener: (event: { data: unknown }) => void): void;
}

/** The page side: a Worker, or anything shaped like one. */
export interface WorkerLike {
  postMessage(message: unknown, transfer?: Transferable[]): void;
  addEventListener(type: 'message' | 'error' | 'messageerror', listener: (event: { data?: unknown; message?: string }) => void): void;
  removeEventListener(type: 'message' | 'error' | 'messageerror', listener: (event: { data?: unknown; message?: string }) => void): void;
  terminate(): void;
}
