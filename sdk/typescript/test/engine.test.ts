// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The integer inference engine on both backends: the greedy and seeded
// tokens of vectors.json, identical in native and wasm; streaming; budget
// binding; cancellation; capability denial; LoRA honesty.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { after, before, describe, test } from 'node:test';

import {
  BudgetExhaustedError, Budgets, Cap, CapabilityDeniedError, type GenerateOptions, Model, NotImplementedError, Rational,
  StopReason, type Zxv, type ZxvContext, ZxvSystemError,
} from '../src/index.js';
import { BACKENDS, fixtures, load, promptText, V } from './helpers.js';

const G = V.generate;
const modelPath = join(fixtures, G.model);
const tokPath = join(fixtures, G.tokenizer);

/** The vectors' seeded sampling parameters as exact SDK options. */
const sampled: GenerateOptions = {
  maxTokens: G.sampled.max_tokens,
  temperature: Rational.fromQ16(G.sampled.temperature_q16),
  topK: G.sampled.top_k,
  topP: Rational.fromQ16(G.sampled.top_p_q16),
  repeatPenalty: Rational.fromQ16(G.sampled.repeat_penalty_q16),
  repeatLastN: G.sampled.repeat_last_n,
  seed: BigInt(G.sampled.seed),
};

const tokensByBackend: Record<string, { greedy: number[]; sampled: number[]; seeds: number[][] }> = {};

for (const kind of BACKENDS) {
  describe(`engine (${kind})`, () => {
    let zxv: Zxv;
    let ctx: ZxvContext;
    let model: Model;

    before(async () => {
      zxv = await load(kind);
      ctx = zxv.createContext();
      model = await Model.open(ctx.rootCapability({ caps: Cap.AI_INFERENCE | Cap.BUDGET }), modelPath, tokPath);
    });
    after(() => ctx.close());

    test('model info and tokenizer round trip', () => {
      assert.equal(model.info().nVocab, 1200);
      const ids = model.tokenize(promptText);
      assert.deepEqual(ids, G.prompt_tokens);
      assert.equal(model.detokenize(ids), promptText);
    });

    test('greedy generation matches vectors.json', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const r = await s.generate(promptText, { maxTokens: G.greedy.max_tokens });
      assert.deepEqual(r.tokens, G.greedy.tokens);
      assert.equal(r.stop, StopReason.MAX_TOKENS);
      assert.equal(r.promptTokens, G.prompt_tokens.length);
      assert.equal(r.generatedTokens, G.greedy.max_tokens);
      assert.equal(r.text, model.detokenize(r.tokens as number[]));
      tokensByBackend[kind] = { greedy: [...r.tokens], sampled: [], seeds: [] };
    });

    test('seeded sampling matches vectors.json', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const r = await s.generate(promptText, sampled);
      assert.deepEqual(r.tokens, G.sampled.tokens);
      tokensByBackend[kind]!.sampled = [...r.tokens];
      for (const seed of [1n, 7n, 123456789n]) {
        s.reset();
        const t = await s.generate(promptText, { ...sampled, seed, temperature: '1.3', topK: 0, topP: '0.9' });
        tokensByBackend[kind]!.seeds.push([...t.tokens]);
      }
    });

    test('streaming yields every token and the same result', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const stream = s.stream(promptText, { maxTokens: G.greedy.max_tokens });
      const seen: number[] = [];
      let text = '';
      for await (const t of stream) {
        seen.push(t.id);
        text += t.text;
      }
      const r = await stream.result;
      assert.deepEqual(seen, G.greedy.tokens);
      assert.equal(text, r.text);
    });

    test('breaking out of the stream cancels generation', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const stream = s.stream(promptText, { maxTokens: 16 });
      let n = 0;
      for await (const _ of stream) if (++n === 3) break;
      const r = await stream.result;
      assert.deepEqual(r.tokens.slice(0, 3), G.greedy.tokens.slice(0, 3));
      if (kind === 'native') {
        assert.equal(r.stop, StopReason.CANCELLED);
        assert.ok(r.generatedTokens >= 3 && r.generatedTokens < 16, `stopped early (${r.generatedTokens})`);
      } else {
        // wasm generates synchronously: the whole run finished before the
        // consumer's break (documented; stop wasm runs from onToken instead).
        assert.equal(r.stop, StopReason.MAX_TOKENS);
      }
    });

    test('onToken returning false and AbortSignal stop generation', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const r = await s.generate(promptText, { maxTokens: 16, onToken: (t) => t.id !== G.greedy.tokens[1] });
      assert.equal(r.stop, StopReason.CANCELLED);
      assert.deepEqual(r.tokens, G.greedy.tokens.slice(0, 2), 'stops exactly after the token that said stop');
      s.reset();
      const ac = new AbortController();
      const r2 = await s.generate(promptText, { maxTokens: 16, signal: ac.signal, onToken: () => { ac.abort(); } });
      assert.equal(r2.stop, StopReason.CANCELLED);
      assert.equal(r2.tokens.length, 1);
      s.reset();
      await assert.rejects(s.generate(promptText, { maxTokens: 16, onToken: () => { throw new Error('consumer bug'); } }), /consumer bug/);
    });

    test('a bound budget stops generation, then refuses the prompt up front', async () => {
      using budgets = Budgets.open(model.cap);
      const b = budgets.create('gen', G.prompt_tokens.length + 8);
      using s = model.newSession({ nCtx: G.n_ctx });
      s.bindBudget(b);
      const r = await s.generate(promptText, { maxTokens: 16 });
      assert.equal(r.stop, StopReason.BUDGET);
      assert.equal(r.tokens.length, 8);
      s.reset();
      await assert.rejects(s.generate(promptText, { maxTokens: 16 }), BudgetExhaustedError);
      s.bindBudget(null);
    });

    test('a session generates one stream at a time', async () => {
      using s = model.newSession({ nCtx: G.n_ctx });
      const first = s.generate(promptText, { maxTokens: 4 });
      if (kind === 'native') assert.throws(() => s.stream(promptText), /generating/);
      await first;
    });

    test('capability denial, missing file, LoRA', async () => {
      const noAi = ctx.rootCapability({ caps: Cap.LEDGER_READ });
      if (kind === 'native') {
        await assert.rejects(Model.open(noAi, modelPath, tokPath), CapabilityDeniedError);
        await assert.rejects(Model.open(model.cap, '/nonexistent.gguf'), ZxvSystemError);
      }
      assert.throws(() => Model.fromBytes(noAi, readFileSync(modelPath)), CapabilityDeniedError);
      assert.throws(() => model.loadLora('adapter.gguf'), NotImplementedError);
      noAi.close();
    });

    if (kind === 'native') {
      test('generation runs off the event loop', async () => {
        using s = model.newSession({ nCtx: 128 });
        let ticks = 0;
        const timer = setInterval(() => ticks++, 0);
        const job = s.generate(promptText, { maxTokens: 100 });
        await new Promise((ok) => setTimeout(ok, 1));
        const r = await job;
        clearInterval(timer);
        assert.ok(r.generatedTokens > 0);
        assert.ok(ticks > 0, 'timers ran while the engine worked');
      });

      test('closing the context while generating waits for the worker', async () => {
        const c2 = zxv.createContext();
        const m2 = Model.fromBytes(c2.rootCapability(), readFileSync(modelPath), readFileSync(tokPath));
        const s2 = m2.newSession({ nCtx: 128 });
        const job = s2.generate(promptText, { maxTokens: 64 });
        c2.close(); // cancels; C handles are destroyed after the worker returns
        const r = await job;
        assert.equal(r.stop, StopReason.CANCELLED);
        assert.ok(s2.closed && m2.closed && c2.closed);
      });
    }
  });
}

describe('determinism across backends', () => {
  test('native and wasm produce identical tokens', () => {
    const n = tokensByBackend['native'];
    const w = tokensByBackend['wasm'];
    assert.ok(n && w, 'both backends ran');
    assert.deepEqual(w.greedy, n.greedy);
    assert.deepEqual(w.sampled, n.sampled);
    assert.deepEqual(w.seeds, n.seeds);
    assert.equal(n.seeds.length, 3);
  });
});
