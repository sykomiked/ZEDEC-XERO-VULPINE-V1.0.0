// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Budget } from './budgets.js';
import type { Capability, ZxvContext } from './context.js';
import { usage } from './errors.js';
import { type GenerateJob, type Ptr, cstr, utf8 } from './internal/backend.js';
import { bytes, i32, str, u32, u64 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';
import { platform } from './internal/platform.js';
import { Rational, toRational } from './rational.js';

/** KV cache precision. */
export const KvCache = Object.freeze({
  /** Exact, 4 bytes per value. */
  Q16: 0,
  /** 8-bit, about 1.16 bytes per value. */
  Q8: 1,
} as const);
/** A KvCache value. */
export type KvCache = (typeof KvCache)[keyof typeof KvCache];

/** Why generation stopped (`ZXV_STOP_*`). */
export const StopReason = Object.freeze({
  EOS: 1,
  MAX_TOKENS: 2,
  STRING: 3,
  /** The consumer stopped it (break, cancel, AbortSignal, or onToken returned false). */
  CANCELLED: 4,
  /** The context window is full. */
  CONTEXT_FULL: 5,
  /** The bound budget ran out. */
  BUDGET: 6,
} as const);
/** A StopReason value. */
export type StopReason = (typeof StopReason)[keyof typeof StopReason];

/** Model facts (`zxv_model_info`). */
export interface ModelInfo {
  /** 1 qwen2, 2 qwen3, 3 llama. */
  readonly arch: number;
  readonly nVocab: number;
  readonly nCtxTrain: number;
  readonly nLayer: number;
  readonly nEmbd: number;
  readonly nHead: number;
  readonly nHeadKv: number;
  /** End-of-sequence token id. */
  readonly eos: number;
}

/** Options of {@link Model.newSession}. */
export interface SessionOptions {
  /** Context window in tokens (0 = min(nCtxTrain, 4096)). */
  nCtx?: number;
  /** KV cache precision (default Q16, exact). */
  kv?: KvCache;
}

/**
 * Sampling and control options. Sampling parameters are exact
 * {@link Rational}s (or exact decimal text), converted to Q16.16 for the
 * engine; there is no float anywhere. The same options and seed give the
 * same tokens on every machine and on both backends.
 */
export interface GenerateOptions {
  /** Upper bound on generated tokens (default 128). */
  maxTokens?: number;
  /** 0 = greedy (default). */
  temperature?: Rational | string;
  /** 0 = off (default). */
  topK?: number;
  /** 0 = off (default). */
  topP?: Rational | string;
  /** 0 = off (default). */
  repeatPenalty?: Rational | string;
  /** Window for the repeat penalty (default 64 when repeatPenalty is set). */
  repeatLastN?: number;
  /** Sampling seed (default 0). */
  seed?: bigint | number;
  /** Cancel generation (stop reason CANCELLED). */
  signal?: AbortSignal;
  /** Called on the event loop for every token; return false to stop. */
  onToken?: (token: GeneratedToken) => boolean | void;
  /** Present this capability instead of the model's. */
  cap?: Capability;
}

/** One generated token. */
export interface GeneratedToken {
  /** Token id. */
  readonly id: number;
  /** Its bytes (UTF-8, possibly part of a character). */
  readonly piece: Uint8Array;
  /** Text completed by this token (a partial character waits for the next). */
  readonly text: string;
}

/** The outcome of a generation. */
export interface GenerateResult {
  readonly text: string;
  readonly tokens: readonly number[];
  readonly stop: StopReason;
  readonly promptTokens: number;
  readonly generatedTokens: number;
}

/** A streaming generation: iterate tokens with `for await`; `result` settles at the end. */
export interface TokenStream extends AsyncIterable<GeneratedToken> {
  readonly result: Promise<GenerateResult>;
  /** Stop after the current token. */
  cancel(): void;
}

/**
 * A GGUF model (qwen2, qwen3, llama; F32/F16/BF16/Q8_0/Q4_0/Q5_x/Q4_K/Q6_K)
 * run by the integer inference engine: bit-identical on every machine. A
 * model is read-only after loading and may be shared by many sessions.
 * Opening needs {@link Cap.AI_INFERENCE}.
 */
export class Model extends Resource {
  /** The capability its sessions present by default. */
  readonly cap: Capability;
  /** @internal */
  readonly _ctx: ZxvContext;
  #ptr: Ptr;

  private constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this._ctx = cap.context;
    this.#ptr = ptr;
  }

  /**
   * Open a GGUF file (and optionally a separate tokenizer GGUF). The native
   * backend reads the file in C; the wasm backend reads it with the host
   * (fs in Node, fetch in a browser) and loads it from memory.
   */
  static async open(cap: Capability, path: string | URL, tokenizerPath?: string | URL): Promise<Model> {
    const core = cap.context._core;
    if (core.b.kind === 'native' && typeof path === 'string' && (tokenizerPath === undefined || typeof tokenizerPath === 'string')) {
      const p = newHandle(core, cap.context._state, 'zxv_model_open', cap._ptr(), cstr(str('path', path)),
        tokenizerPath === undefined ? null : cstr(tokenizerPath));
      return new Model(cap, p);
    }
    const pf = platform();
    const m = await pf.readFile(path);
    const t = tokenizerPath === undefined ? undefined : await pf.readFile(tokenizerPath);
    return Model.fromBytes(cap, m, t);
  }

  /** Load from bytes (copied by the engine; the arrays may be reused). */
  static fromBytes(cap: Capability, model: Uint8Array, tokenizer?: Uint8Array): Model {
    const m = bytes('model', model);
    const t = tokenizer === undefined ? null : bytes('tokenizer', tokenizer);
    const c = cap.context;
    return new Model(cap, newHandle(c._core, c._state, 'zxv_model_open_memory', cap._ptr(), m, m.length, t, t ? t.length : 0));
  }

  /** @internal */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  #info(key: number): number {
    const v = new BigInt64Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_model_info', this._p(), key, v);
    return Number(v[0] ?? 0n);
  }

  /** Architecture and dimensions. */
  info(): ModelInfo {
    return Object.freeze({
      arch: this.#info(1), nVocab: this.#info(2), nCtxTrain: this.#info(3), nLayer: this.#info(4),
      nEmbd: this.#info(5), nHead: this.#info(6), nHeadKv: this.#info(7), eos: this.#info(8),
    });
  }

  /** Token ids of text (`parseSpecial`: recognise special tokens like <|im_start|>; default true). */
  tokenize(text: string, options: { parseSpecial?: boolean } = {}): number[] {
    const t = utf8(str('text', text));
    const special = options.parseSpecial === false ? 0 : 1;
    const c = this._ctx._core;
    const count = c.b.sizeSlot();
    let s = c.callAllowing(this._ctx._state, [-2], 'zxv_tokenize', this._p(), t, t.length, special, null, 0, count);
    if (s === 0) return [];
    const ids = new Int32Array(c.b.readSize(count));
    s = c.callAllowing(this._ctx._state, [], 'zxv_tokenize', this._p(), t, t.length, special, ids, ids.length, count);
    return Array.from(ids.subarray(0, c.b.readSize(count)));
  }

  /** Text of token ids. */
  detokenize(ids: readonly number[]): string {
    const a = Int32Array.from(ids, (x, i) => i32(`ids[${i}]`, x));
    return this._ctx._core.text(this._ctx._state, 'zxv_detokenize', (b, cap, l) => [this._p(), a, a.length, b, cap, l]);
  }

  /** A new conversation (KV cache). */
  newSession(options: SessionOptions = {}): Session {
    const p = newHandle(this._ctx._core, this._ctx._state, 'zxv_session_create', this._p(), u32('nCtx', options.nCtx ?? 0),
      i32('kv', options.kv ?? KvCache.Q16));
    return new Session(this, p);
  }

  /**
   * Load a LoRA adapter. NOT BUILT YET: the engine reports
   * ZXV_FEAT_LORA off, and this throws {@link NotImplementedError}.
   */
  loadLora(path: string, scale: Rational | string = Rational.ONE): never {
    const slot = this._ctx._core.b.handleSlot();
    this._ctx._core.call(this._ctx._state, 'zxv_lora_load', this.cap._ptr(), this._p(), cstr(str('path', path)),
      toRational('scale', scale).toQ16(), slot);
    // A future library that implements LoRA would get here; release it and refuse until the SDK supports it.
    this._ctx._core.b.call('zxv_lora_destroy', this._ctx._core.b.readHandle(slot));
    throw usage('LoRA adapters are not supported by this SDK version');
  }

  protected destroy(): void {
    const p = this.#ptr;
    this._ctx._state.release(() => this._ctx._core.b.call('zxv_model_destroy', p));
  }
}

/**
 * One conversation with a model: a KV cache that continues turn after
 * turn. Use it for one generation at a time.
 *
 * On the native backend generation runs on a worker thread and tokens
 * stream back to the event loop, which is never blocked. On the wasm
 * backend it runs synchronously on the calling thread: in a browser, run
 * it inside a Web Worker.
 */
export class Session extends Resource {
  /** The model. */
  readonly model: Model;
  #ptr: Ptr;
  #busy = false;
  #cancelCurrent: (() => void) | null = null;
  #budget: Budget | null = null;

  /** @internal */
  constructor(model: Model, ptr: Ptr) {
    super(model);
    this.model = model;
    this.#ptr = ptr;
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Forget the conversation. */
  reset(): void {
    this.#idle();
    this.model._ctx._core.call(this.model._ctx._state, 'zxv_session_reset', this.#p());
  }

  /** Tokens currently in the KV cache. */
  position(): number {
    const p = new Uint32Array(1);
    this.model._ctx._core.call(this.model._ctx._state, 'zxv_session_position', this.#p(), p);
    return p[0] ?? 0;
  }

  /**
   * Charge `budget` one unit per prompt and generated token (null
   * unbinds). Prompt tokens are charged up front: if they do not fit,
   * generation throws BudgetExhaustedError and runs nothing; if the budget
   * runs out mid-way, generation stops with {@link StopReason.BUDGET}.
   */
  bindBudget(budget: Budget | null): void {
    this.#idle();
    const c = this.model._ctx;
    if (budget === null) c._core.call(c._state, 'zxv_session_bind_budget', this.#p(), null, 0);
    else c._core.call(c._state, 'zxv_session_bind_budget', this.#p(), budget.budgets._p(), budget.id);
    this.#budget = budget;
  }

  /** The bound budget, if any. */
  get budget(): Budget | null {
    return this.#budget;
  }

  #idle(): void {
    if (this.#busy) throw usage('the session is generating; await it or cancel first');
  }

  /** Generate and resolve with the whole result. */
  async generate(prompt: string, options: GenerateOptions = {}): Promise<GenerateResult> {
    const s = this.stream(prompt, options);
    for await (const _ of s) {
      // drain; onToken (if any) already ran
    }
    return s.result;
  }

  /** Generate as an async stream of tokens. */
  stream(prompt: string, options: GenerateOptions = {}): TokenStream {
    this.#idle();
    const ctx = this.model._ctx;
    const cap = options.cap ?? this.model.cap;
    const temperature = toRational('temperature', options.temperature ?? Rational.ZERO);
    const topP = toRational('topP', options.topP ?? Rational.ZERO);
    const repeat = toRational('repeatPenalty', options.repeatPenalty ?? Rational.ZERO);
    const call = {
      cap: cap._ptr(),
      session: this.#p(),
      prompt: utf8(str('prompt', prompt)),
      maxTokens: u32('maxTokens', options.maxTokens ?? 128),
      temperatureQ16: temperature.toQ16(),
      topK: u32('topK', options.topK ?? 0),
      topPQ16: topP.toQ16(),
      repeatPenaltyQ16: repeat.toQ16(),
      repeatLastN: u32('repeatLastN', options.repeatLastN ?? (repeat.sign() === 0 ? 0 : 64)),
      seed: u64('seed', options.seed ?? 0n),
    };
    options.signal?.throwIfAborted();

    const queue: GeneratedToken[] = [];
    const ids: number[] = [];
    let text = '';
    let finished = false;
    let failure: unknown = undefined;
    let notify: (() => void) | null = null;
    const decoder = new TextDecoder('utf-8');
    let job: GenerateJob | undefined;
    let stopRequested = false;
    let callbackError: { error: unknown } | undefined;
    const wake = () => {
      const n = notify;
      notify = null;
      n?.();
    };
    const cancel = () => {
      stopRequested = true;
      job?.cancel();
    };
    const onToken = (id: number, piece: Uint8Array) => {
      const t: GeneratedToken = Object.freeze({ id, piece, text: decoder.decode(piece, { stream: true }) });
      ids.push(id);
      text += t.text;
      queue.push(t);
      try {
        if (options.onToken && options.onToken(t) === false) cancel();
      } catch (e) {
        callbackError ??= { error: e };
        cancel();
      }
      wake();
      return !stopRequested;
    };

    this.#busy = true;
    this.#cancelCurrent = cancel;
    ctx._state.begin();
    const onAbort = () => cancel();
    options.signal?.addEventListener('abort', onAbort);
    let result: Promise<GenerateResult>;
    try {
      job = ctx._core.b.generate(call, onToken);
      if (stopRequested) job.cancel();
      result = job.done.then((r) => {
        text += decoder.decode();
        if (callbackError) throw callbackError.error;
        if (r.status !== 0) ctx._core.fail(r.status, ctx._state, r.detail);
        return Object.freeze({
          text, tokens: Object.freeze([...ids]), stop: r.stop as StopReason, promptTokens: r.nPrompt, generatedTokens: r.nGenerated,
        });
      });
    } catch (e) {
      result = Promise.reject(e);
    }
    const settled = result.then(
      () => undefined,
      (e: unknown) => { failure = e; },
    ).finally(() => {
      finished = true;
      this.#busy = false;
      this.#cancelCurrent = null;
      options.signal?.removeEventListener('abort', onAbort);
      ctx._state.end();
      wake();
    });
    void settled;
    result.catch(() => undefined); // the iterator or the caller observes it

    const iterator: AsyncIterator<GeneratedToken> = {
      next: async (): Promise<IteratorResult<GeneratedToken>> => {
        for (;;) {
          const t = queue.shift();
          if (t) return { value: t, done: false };
          if (finished) {
            if (failure !== undefined) throw failure;
            return { value: undefined, done: true };
          }
          await new Promise<void>((ok) => { notify = ok; });
        }
      },
      return: async (): Promise<IteratorResult<GeneratedToken>> => {
        if (!finished) cancel();
        await settled;
        return { value: undefined, done: true };
      },
    };
    return {
      result,
      cancel,
      [Symbol.asyncIterator]: () => iterator,
    };
  }

  protected destroy(): void {
    this.#cancelCurrent?.(); // a running generation stops; the destroy waits for it
    const p = this.#ptr;
    this.model._ctx._state.release(() => this.model._ctx._core.b.call('zxv_session_destroy', p));
  }
}

